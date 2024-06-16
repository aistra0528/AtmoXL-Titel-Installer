#include <filesystem>
#include <vector>
#include <algorithm>
#include <fstream>
#include <unistd.h>
#include <curl/curl.h>
#include <regex>
#include <arpa/inet.h>
#include <unistd.h>
#include "switch.h"
#include "util/util.hpp"
#include "nx/ipc/tin_ipc.h"
#include "util/config.hpp"
#include "util/curl.hpp"
#include "ui/MainApplication.hpp"
#include "util/usb_comms_awoo.h"
#include "util/json.hpp"
#include "nx/usbhdd.h"
#include "util/error.hpp"

namespace inst::util {
    void initApp () {
        if (!std::filesystem::exists("sdmc:/config")) std::filesystem::create_directory("sdmc:/config");
        if (!std::filesystem::exists(inst::config::appDir)) std::filesystem::create_directory(inst::config::appDir);
        inst::config::parseConfig();
        inst::config::parseThemeColorConfig();

        socketInitializeDefault();
        #ifdef __DEBUG__
            nxlinkStdio();
        #endif
        awoo_usbCommsInitialize();

		nx::hdd::init();

        if(R_FAILED(ncmInitialize()))
            LOG_DEBUG("Failed to initialize ncm\n");
    }

    void deinitApp () {
        ncmExit();

		nx::hdd::exit();
        socketExit();
        awoo_usbCommsExit();
    }

    void reinitUsbComms() {
        awoo_usbCommsExit();
        awoo_usbCommsInitialize();
    }

    void initInstallServices() {
        ncmInitialize();
        nsextInitialize();
        esInitialize();
        splCryptoInitialize();
        splInitialize();
    }

    void deinitInstallServices() {
        ncmExit();
        nsextExit();
        esExit();
        splCryptoExit();
        splExit();
    }

    //struct caseInsensitiveLess : public std::binary_function< char,char,bool > {
        //bool operator () (char x, char y) const {
            //return toupper(static_cast< unsigned char >(x)) < toupper(static_cast< unsigned char >(y));
        //}
    //};

    auto caseInsensitiveLess = [](auto& x, auto& y)->bool { 
        return toupper(static_cast<unsigned char>(x)) < toupper(static_cast<unsigned char>(y));
    };

    bool ignoreCaseCompare(const std::string &a, const std::string &b) {
        return std::lexicographical_compare(a.begin(), a.end() , b.begin() ,b.end() , caseInsensitiveLess);
    }

    std::vector<std::filesystem::path> getDirectoryFiles(const std::string & dir, const std::vector<std::string> & extensions) {
        std::vector<std::filesystem::path> files;
        for(auto & p: std::filesystem::directory_iterator(dir))
        {
            try {
            if (std::filesystem::is_regular_file(p))
            {
                std::string ourExtension = p.path().extension().string();
                std::transform(ourExtension.begin(), ourExtension.end(), ourExtension.begin(), ::tolower);
                if (extensions.empty() || std::find(extensions.begin(), extensions.end(), ourExtension) != extensions.end())
                {
                    files.push_back(p.path());
                }
            }
         } catch (std::filesystem::filesystem_error & e) {}
        }
        std::sort(files.begin(), files.end(), ignoreCaseCompare);
        return files;
    }

    std::vector<std::filesystem::path> getDirsAtPath(const std::string & dir) {
        std::vector<std::filesystem::path> files;
        for(auto & p: std::filesystem::directory_iterator(dir))
        {
         try {
            if (std::filesystem::is_directory(p))
            {
                    files.push_back(p.path());
            }
         } catch (std::filesystem::filesystem_error & e) {}
        }
        std::sort(files.begin(), files.end(), ignoreCaseCompare);
        return files;
    }

    bool removeDirectory(std::string dir) {
        try {
            for(auto & p: std::filesystem::recursive_directory_iterator(dir))
            {
                if (std::filesystem::is_regular_file(p))
                {
                    std::filesystem::remove(p);
                }
            }
            rmdir(dir.c_str());
            return true;
        }
        catch (std::filesystem::filesystem_error & e) {
            return false;
        }
    }

    bool copyFile(std::string inFile, std::string outFile) {
       char ch;
       std::ifstream f1(inFile);
       std::ofstream f2(outFile);

       if(!f1 || !f2) return false;
       
       while(f1 && f1.get(ch)) f2.put(ch);
       return true;
    }

    std::string formatUrlString(std::string ourString) {
        std::stringstream ourStream(ourString);
        std::string segment;
        std::vector<std::string> seglist;

        while(std::getline(ourStream, segment, '/')) {
            seglist.push_back(segment);
        }

        CURL *curl = curl_easy_init();
        int outlength;
        std::string finalString = curl_easy_unescape(curl, seglist[seglist.size() - 1].c_str(), seglist[seglist.size() - 1].length(), &outlength);
        curl_easy_cleanup(curl);

        return finalString;
    }

    std::string formatUrlLink(std::string ourString){
        std::string::size_type pos = ourString.find('/');
        if (pos != std::string::npos)
            return ourString.substr(0, pos);
        else
            return ourString;
    }

    std::string shortenString(std::string ourString, int ourLength, bool isFile) {
        std::filesystem::path ourStringAsAPath = ourString;
        std::string ourExtension = ourStringAsAPath.extension().string();
        if (ourString.size() - ourExtension.size() > (unsigned long)ourLength) {
            if(isFile) return (std::string)ourString.substr(0,ourLength) + "(...)" + ourExtension;
            else return (std::string)ourString.substr(0,ourLength) + "...";
        } else return ourString;
    }

    std::string readTextFromFile(std::string ourFile) {
        if (std::filesystem::exists(ourFile)) {
            FILE * file = fopen(ourFile.c_str(), "r");
            char line[1024];
            fgets(line, 1024, file);
            std::string url = line;
            fflush(file);
            fclose(file);
            return url;
        }
        return "";
    }

    std::string softwareKeyboard(std::string guideText, std::string initialText, int LenMax) {
        Result rc=0;
        SwkbdConfig kbd;
        char tmpoutstr[LenMax + 1] = {0};
        rc = swkbdCreate(&kbd, 0);
        if (R_SUCCEEDED(rc)) {
            swkbdConfigMakePresetDefault(&kbd);
            swkbdConfigSetGuideText(&kbd, guideText.c_str());
            swkbdConfigSetInitialText(&kbd, initialText.c_str());
            swkbdConfigSetStringLenMax(&kbd, LenMax);
            rc = swkbdShow(&kbd, tmpoutstr, sizeof(tmpoutstr));
            swkbdClose(&kbd);
            if (R_SUCCEEDED(rc) && tmpoutstr[0] != 0) return(((std::string)(tmpoutstr)));
        }
        return "";
    }

    std::string getDriveFileName(std::string fileId) {
        std::string htmlData = inst::curl::downloadToBuffer("https://drive.google.com/file/d/" + fileId  + "/view");
        if (htmlData.size() > 0) {
            std::smatch ourMatches;
            std::regex ourRegex("<title>\\s*(.+?)\\s*</title>");
            std::regex_search(htmlData, ourMatches, ourRegex);
            if (ourMatches.size() > 1) {
                if (ourMatches[1].str() == "Google Drive -- Page Not Found") return "";
                return ourMatches[1].str().substr(0, ourMatches[1].str().size() - 15);
             }
        }
        return "";
    }

    std::vector<uint32_t> setClockSpeed(int deviceToClock, uint32_t clockSpeed) {
        uint32_t hz = 0;
        uint32_t previousHz = 0;

        if (deviceToClock > 2 || deviceToClock < 0) return {0,0};

        if(hosversionAtLeast(8,0,0)) {
            ClkrstSession session = {0};
            PcvModuleId pcvModuleId;
            pcvInitialize();
            clkrstInitialize();

            switch (deviceToClock) {
                case 0:
                    pcvGetModuleId(&pcvModuleId, PcvModule_CpuBus);
                    break;
                case 1:
                    pcvGetModuleId(&pcvModuleId, PcvModule_GPU);
                    break;
                case 2:
                    pcvGetModuleId(&pcvModuleId, PcvModule_EMC);
                    break;
            }

            clkrstOpenSession(&session, pcvModuleId, 3);
            clkrstGetClockRate(&session, &previousHz);
            clkrstSetClockRate(&session, clockSpeed);
            clkrstGetClockRate(&session, &hz);

            pcvExit();
            clkrstCloseSession(&session);
            clkrstExit();

            return {previousHz, hz};
        } else {
            PcvModule pcvModule;
            pcvInitialize();

            switch (deviceToClock) {
                case 0:
                    pcvModule = PcvModule_CpuBus;
                    break;
                case 1:
                    pcvModule = PcvModule_GPU;
                    break;
                case 2:
                    pcvModule = PcvModule_EMC;
                    break;
            }

            pcvGetClockRate(pcvModule, &previousHz);
            pcvSetClockRate(pcvModule, clockSpeed);
            pcvGetClockRate(pcvModule, &hz);
            
            pcvExit();

            return {previousHz, hz};
        }
    }

    std::string getIPAddress() {
        struct in_addr addr = {(in_addr_t) gethostid()};
        return inet_ntoa(addr);
    }
    
    bool usbIsConnected() {
        UsbState state = UsbState_Detached;
        usbDsGetState(&state);
        return state == UsbState_Configured;
    }

    void playAudio(std::string audioPath) {
        int audio_rate = 22050;
        Uint16 audio_format = AUDIO_S16SYS;
        int audio_channels = 2;
        int audio_buffers = 4096;

        if(Mix_OpenAudio(audio_rate, audio_format, audio_channels, audio_buffers) != 0) return;

        Mix_Chunk *sound = NULL;
        sound = Mix_LoadWAV(audioPath.c_str());
        if(sound == NULL || !inst::config::enableSound) {
            Mix_FreeChunk(sound);
            Mix_CloseAudio();
            return;
        }

        int channel = Mix_PlayChannel(-1, sound, 0);
        if(channel == -1) {
            Mix_FreeChunk(sound);
            Mix_CloseAudio();
            return;
        }

        while(Mix_Playing(channel) != 0);

        Mix_FreeChunk(sound);
        Mix_CloseAudio();

        return;
    }
    
    void lightningStart() {
        padConfigureInput(8, HidNpadStyleSet_NpadStandard);
        PadState pad;
        padInitializeDefault(&pad);
        padUpdate(&pad);

        Result rc=0;
        s32 i;
        s32 total_entries;
        HidsysUniquePadId unique_pad_ids[2]={0};
        HidsysNotificationLedPattern pattern;

        rc = hidsysInitialize();
        if (R_SUCCEEDED(rc)) {
            memset(&pattern, 0, sizeof(pattern));

            pattern.baseMiniCycleDuration = 0x1;
            pattern.totalMiniCycles = 0xF;
            pattern.totalFullCycles = 0x0;
            pattern.startIntensity = 0x0;

            pattern.miniCycles[0].ledIntensity = 0xF;
            pattern.miniCycles[0].transitionSteps = 0xF;
            pattern.miniCycles[0].finalStepDuration = 0x0;
            pattern.miniCycles[1].ledIntensity = 0x0;
            pattern.miniCycles[1].transitionSteps = 0xF;
            pattern.miniCycles[1].finalStepDuration = 0x0;
            pattern.miniCycles[2].ledIntensity = 0xF;
            pattern.miniCycles[2].transitionSteps = 0xF;
            pattern.miniCycles[2].finalStepDuration = 0x0;
            pattern.miniCycles[3].ledIntensity = 0x0;
            pattern.miniCycles[3].transitionSteps = 0xF;
            pattern.miniCycles[3].finalStepDuration = 0x0;

            total_entries = 0;
            memset(unique_pad_ids, 0, sizeof(unique_pad_ids));
            rc = hidsysGetUniquePadsFromNpad(padIsHandheld(&pad) ? HidNpadIdType_Handheld : HidNpadIdType_No1, unique_pad_ids, 2, &total_entries);

            if (R_SUCCEEDED(rc)) {
                for(i=0; i<total_entries; i++) {
                    rc = hidsysSetNotificationLedPattern(&pattern, unique_pad_ids[i]);
                }
            }
        }
    }
    
    void lightningStop() {
        padConfigureInput(8, HidNpadStyleSet_NpadStandard);
        PadState pad;
        padInitializeDefault(&pad);
        padUpdate(&pad);

        Result rc=0;
        s32 i;
        s32 total_entries;
        HidsysUniquePadId unique_pad_ids[2]={0};
        HidsysNotificationLedPattern pattern;

        rc = hidsysInitialize();
        if (R_SUCCEEDED(rc)) {
            memset(&pattern, 0, sizeof(pattern));
            
            total_entries = 0;
            memset(unique_pad_ids, 0, sizeof(unique_pad_ids));
            rc = hidsysGetUniquePadsFromNpad(padIsHandheld(&pad) ? HidNpadIdType_Handheld : HidNpadIdType_No1, unique_pad_ids, 2, &total_entries);

            if (R_SUCCEEDED(rc)) {
                for(i=0; i<total_entries; i++) {
                    rc = hidsysSetNotificationLedPattern(&pattern, unique_pad_ids[i]);
                }
            }
        }
    }

    std::string* getBatteryCharge() {
        std::string batColBlue = "#0000FFFF";
        std::string batColGreen = "#00FF00FF";
        std::string batColYellow = "#FFFF00FF";
        std::string batColOrange = "#FF8000FF";
        std::string batColRed = "#FF0000FF";
        std::string* batValue = new std::string[2];
        batValue[0] = "???";
        batValue[1] = batColBlue;
        u32 charge;

        Result rc = psmInitialize();
        if (!R_FAILED(rc)) {
            rc = psmGetBatteryChargePercentage(&charge);
            if (!R_FAILED(rc)) {
            if (charge < 15.0) {
                batValue[1] = batColRed;
            } else if (charge < 30.0) {
                batValue[1] = batColOrange;
            } else if (charge < 50.0) {
                batValue[1] = batColYellow;
            } else {
                batValue[1] = batColGreen;
            }
                batValue[0] = std::to_string(charge) + "%";
            }
        }
        psmExit();
        return batValue;
    }

    std::vector<std::pair<u64, u32>> listInstalledTitles() {
        std::vector<std::pair<u64, u32>> installedTitles = {};
        const NcmStorageId storageIDs[]{NcmStorageId_SdCard, NcmStorageId_BuiltInUser};
        for (const auto storageID : storageIDs) {
            NcmContentMetaDatabase metaDatabase = {};
            if(R_SUCCEEDED(ncmOpenContentMetaDatabase(&metaDatabase, storageID))) {
                auto metaKeys = new NcmContentMetaKey[64000]();
                s32 written = 0;
                s32 total = 0;
                if(R_SUCCEEDED(ncmContentMetaDatabaseList(&metaDatabase, &total, &written, metaKeys, 64000, NcmContentMetaType_Unknown, 0, 0, UINT64_MAX, NcmContentInstallType_Full)) && (written > 0))
                    for(s32 i = 0; i < written; i++) {
                        const auto &metaKey = metaKeys[i];
                        installedTitles.push_back({metaKey.id, metaKey.version});
                    }
                delete[] metaKeys;
                ncmContentMetaDatabaseClose(&metaDatabase);
            }
        }
        return installedTitles;
    }
    
    bool isTitleInstalled(std::string filename, const std::vector<std::pair<u64, u32>> &installedTitles) {
        static const std::regex idRegex(".*\\[([0-9a-fA-F]+)]\\[v(\\d+)].*");
        std::smatch match;
        if (std::regex_match(filename, match, idRegex)) {
            u64 id = stol(match[1], nullptr, 16);
            u32 version = stoi(match[2]);
            for (const auto &title: installedTitles)
                if (id == title.first and version <= title.second)
                    return true;
        }
        return false;
    }

   std::vector<std::string> checkForAppUpdate () {
        try {
            std::string jsonData = inst::curl::downloadToBuffer("https://api.github.com/repos/ghost/AtmoXL-Titel-Installer/releases/latest", 0, 0, 1000L);
            if (jsonData.size() == 0) return {};
            nlohmann::json ourJson = nlohmann::json::parse(jsonData);
            if (ourJson["tag_name"].get<std::string>() != inst::config::appVersion) {
                std::vector<std::string> ourUpdateInfo = {ourJson["tag_name"].get<std::string>(), ourJson["assets"][0]["browser_download_url"].get<std::string>()};
                inst::config::updateInfo = ourUpdateInfo;
                return ourUpdateInfo;
            }
        } catch (...) {}
        return {};
    }
}
