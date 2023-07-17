#pragma once

#ifndef CONFIGFILE_H
#define CONFIGFILE_H

#include <string>
#include <vector>

struct strConfigParaSection {
    bool bActive = true;
    std::string name = "";
    std::string nameLegacy1 = "";
};

struct strConfigParaString {
    bool bActive = true;
    std::string name = "";
    std::string nameLegacy1 = "";
    std::string sValueDefault = "";
    std::string sValue = "";
};

struct strConfigParaBool {
    bool bActive = true;
    std::string name = "";
    std::string nameLegacy1 = "";
    bool bValueDefault = false;
    bool bValue = false;
};

struct strConfigParaInt {
    bool bActive = true;
    std::string name = "";
    std::string nameLegacy1 = "";
    int iValueDefault = 0;
    int iValue = 0;
    int iMaxValue = 0;
    int iMinValue = 0;
};

struct strConfigParaFloat {
    bool bActive = true;
    std::string name = "";
    std::string nameLegacy1 = "";
    float fValueDefault = 0.0;
    float fValue = 0.0;
    float fMaxValue = 0.0;
    float fminValue = 0.0;
};


struct strConfigIni {
    strConfigParaSection SectionTakeImage;
    strConfigParaString  SectionTakeImage_RawImagesLocation;
    strConfigParaInt     SectionTakeImage_RawImagesRetention;
    strConfigParaFloat   SectionTakeImage_WaitBeforeTakingPicture;
    strConfigParaString  SectionTakeImage_ImageQuality;
};
extern struct strConfigIni ConfigIni;


class ConfigFile {
public:
    ConfigFile(std::string filePath);
    ~ConfigFile();

    bool isNewParagraph(std::string input);
    bool GetNextParagraph(std::string& aktparamgraph, bool &disabled, bool &eof);
	bool getNextLine(std::string* rt, bool &disabled, bool &eof);
    bool ConfigFileExists(){return pFile;};
    
private:
    FILE* pFile;
};

void migrateConfiguration();

#endif //CONFIGFILE_H