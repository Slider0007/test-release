#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_log.h>

#include "Helper.h"
#include "configFile.h"
#include "ClassLogFile.h"

#include "../../include/defines.h"

static const char *TAG = "CONFIG_INI";

ConfigFile::ConfigFile(std::string filePath)
{
    std::string config = FormatFileName(filePath);
    pFile = fopen(config.c_str(), "r");
}


ConfigFile::~ConfigFile()
{
    fclose(pFile);
}


bool ConfigFile::isNewParagraph(std::string input)
{
	if ((input[0] == '[') || ((input[0] == ';') && (input[1] == '[')))
	{
		return true;
	}
	return false;
}


bool ConfigFile::GetNextParagraph(std::string& aktparamgraph, bool &disabled, bool &eof)
{
	while (getNextLine(&aktparamgraph, disabled, eof) && !isNewParagraph(aktparamgraph));

	if (isNewParagraph(aktparamgraph))
		return true;
	return false;
}


bool ConfigFile::getNextLine(std::string *rt, bool &disabled, bool &eof)
{
    eof = false;
	char zw[256] = "";
	if (pFile == NULL)
	{
		*rt = "";
		return false;
	}

	if (fgets(zw, sizeof(zw), pFile))
	{
		ESP_LOGD(TAG, "%s", zw);
		if ((strlen(zw) == 0) && feof(pFile))
		{
			*rt = "";
			eof = true;
			return false;
		}
	}
	else
	{
		*rt = "";
		eof = true;
		return false;
	}
	*rt = zw;
	*rt = trim(*rt);
	while ((zw[0] == ';' || zw[0] == '#' || (rt->size() == 0)) && !(zw[1] == '['))
	{
		fgets(zw, sizeof(zw), pFile);
		ESP_LOGD(TAG, "%s", zw);
		if (feof(pFile))
		{
			*rt = "";
            eof = true;
			return false;
		}
		*rt = zw;
		*rt = trim(*rt);
	}

    disabled = ((*rt)[0] == ';');
	return true;
}


void initConfigIniDefault(void)
{
    ConfigIni.SectionTakeImage.bActive = true;
    ConfigIni.SectionTakeImage.name = "TakeImage";
    ConfigIni.SectionTakeImage.nameLegacy1 = "MakeImage";

    ConfigIni.SectionTakeImage_RawImagesLocation.bActive = true;
    ConfigIni.SectionTakeImage_RawImagesLocation.name = "RawImagesLocation";
    ConfigIni.SectionTakeImage_RawImagesLocation.nameLegacy1 = "LogImageLocation";
    ConfigIni.SectionTakeImage_RawImagesLocation.sValueDefault = "/log/source";

    ConfigIni.SectionTakeImage_RawImagesRetention.bActive = true;
    ConfigIni.SectionTakeImage_RawImagesRetention.name = "RawImagesRetention";
    ConfigIni.SectionTakeImage_RawImagesRetention.nameLegacy1 = "LogfileRetentionInDays";
    ConfigIni.SectionTakeImage_RawImagesRetention.iValueDefault = 5;
}


void readConfigIni(void)
{
    std::string section = "";
	std::ifstream ifs(CONFIG_FILE);
  	std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
	
    /* Split config file it array of lines */
    std::vector<std::string> configLines = splitString(content);

    /* Process each line */
    for (int i = 0; i < configLines.size(); i++) {
        //ESP_LOGI(TAG, "Line %d: %s", i, configLines[i].c_str());
        
    }
}


void migrateConfiguration(void)
{
    bool migrated = false;

    if (!FileExists(CONFIG_FILE)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Config file seems to be missing");
        return;	
    }

    std::string section = "";
	std::ifstream ifs(CONFIG_FILE);
  	std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
	
    /* Split config file it array of lines */
    std::vector<std::string> configLines = splitString(content);

    /* Process each line */
    for (int i = 0; i < configLines.size(); i++) {
        //ESP_LOGI(TAG, "Line %d: %s", i, configLines[i].c_str());

        if (configLines[i].find("[") != std::string::npos) { // Start of new section
            section = configLines[i];
            replaceString(section, ";", "", false); // Remove possible semicolon (just for the string comparison)
            //ESP_LOGI(TAG, "New section: %s", section.c_str());
        }

        /* Migrate parameters as needed
         * For all boolean and further more parameter, we make them enabled all the time now:
         *  1. If they where disabled, set them to their default value
         *  2. Enable them
         * Notes:
         * The migration has some simplifications:
         *  - Case Sensitiveness must be like in the initial config.ini
         *  - No Whitespace after a semicollon
         *  - Only one whitespace before/after the equal sign
         */
        if (section == "[MakeImage]") {
            migrated = migrated | replaceString(configLines[i], "[MakeImage]", "[TakeImage]"); // Rename the section itself
        }

        if (section == "[MakeImage]" || section == "[TakeImage]") {
            migrated = migrated | replaceString(configLines[i], "LogImageLocation", "RawImagesLocation");
            migrated = migrated | replaceString(configLines[i], "LogfileRetentionInDays", "RawImagesRetention");

            migrated = migrated | replaceString(configLines[i], ";FixedExposure = true", ";FixedExposure = false"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";FixedExposure", "FixedExposure"); // Enable it

            migrated = migrated | replaceString(configLines[i], ";Demo = true", ";Demo = false"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";Demo", "Demo"); // Enable it
        }

        if (section == "[Alignment]") {
            if (isInString(configLines[i], "AlignmentAlgo") && isInString(configLines[i], ";")) { // It is the parameter "AlignmentAlgo" and it is commented out
                migrated = migrated | replaceString(configLines[i], "highAccuracy", "default"); // Set it to its default value and enable it
                migrated = migrated | replaceString(configLines[i], "fast", "default"); // Set it to its default value and enable it
                migrated = migrated | replaceString(configLines[i], "off", "default"); // Set it to its default value and enable it
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            migrated = migrated | replaceString(configLines[i], ";FlipImageSize = true", ";FlipImageSize = false"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";FlipImageSize", "FlipImageSize"); // Enable it

            migrated = migrated | replaceString(configLines[i], ";InitialMirror = true", ";InitialMirror = false"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";InitialMirror", "InitialMirror"); // Enable it
        }

        if (section == "[Digits]") {
            if (isInString(configLines[i], "CNNGoodThreshold")) { // It is the parameter "CNNGoodThreshold"
                migrated = migrated | replaceString(configLines[i], "0.5", "0.0");
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }
            migrated = migrated | replaceString(configLines[i], "LogImageLocation", "ROIImagesLocation");
            migrated = migrated | replaceString(configLines[i], "LogfileRetentionInDays", "ROIImagesRetention");
        }

        if (section == "[Analog]") {
            migrated = migrated | replaceString(configLines[i], "LogImageLocation", "ROIImagesLocation");
            migrated = migrated | replaceString(configLines[i], "LogfileRetentionInDays", "ROIImagesRetention");
            migrated = migrated | replaceString(configLines[i], "CNNGoodThreshold", ";UNUSED_PARAMETER"); // This parameter is no longer used          
            migrated = migrated | replaceString(configLines[i], "ExtendedResolution", ";UNUSED_PARAMETER"); // This parameter is no longer used
        }

        if (section == "[PostProcessing]") {
            migrated = migrated | replaceString(configLines[i], ";PreValueUse = true", ";PreValueUse = false"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";PreValueUse", "PreValueUse"); // Enable it

            migrated = migrated | replaceString(configLines[i], ";PreValueAgeStartup", "PreValueAgeStartup"); // Enable it

            migrated = migrated | replaceString(configLines[i], ";ErrorMessage = true", ";ErrorMessage = false"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";ErrorMessage", "ErrorMessage"); // Enable it

            migrated = migrated | replaceString(configLines[i], ";CheckDigitIncreaseConsistency = true", ";CheckDigitIncreaseConsistency = false"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";CheckDigitIncreaseConsistency", "CheckDigitIncreaseConsistency"); // Enable it

            if (isInString(configLines[i], "DecimalShift") && isInString(configLines[i], ";")) { // It is the parameter "DecimalShift" and it is commented out
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            /* AllowNegativeRates has a <NUMBER> as prefix! */
            if (isInString(configLines[i], "AllowNegativeRates") && isInString(configLines[i], ";")) { // It is the parameter "AllowNegativeRates" and it is commented out
                migrated = migrated | replaceString(configLines[i], "true", "false"); // Set it to its default value
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            if (isInString(configLines[i], "AnalogDigitalTransitionStart") && isInString(configLines[i], ";")) { // It is the parameter "AnalogDigitalTransitionStart" and it is commented out
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            /* MaxRateType has a <NUMBER> as prefix! */
            if (isInString(configLines[i], "MaxRateType") && isInString(configLines[i], ";")) { // It is the parameter "MaxRateType" and it is commented out
                migrated = migrated | replaceString(configLines[i], "Off", "AbsoluteChange"); // Set it to its default value and enable it
                migrated = migrated | replaceString(configLines[i], "RateChange", "AbsoluteChange"); // Set it to its default value and enable it
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            if (isInString(configLines[i], "MaxRateValue") && isInString(configLines[i], ";")) { // It is the parameter "MaxRateValue" and it is commented out
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            /* ExtendedResolution has a <NUMBER> as prefix! */
            if (isInString(configLines[i], "ExtendedResolution") && isInString(configLines[i], ";")) { // It is the parameter "ExtendedResolution" and it is commented out
                migrated = migrated | replaceString(configLines[i], "true", "false"); // Set it to its default value
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }
        
            /* IgnoreLeadingNaN has a <NUMBER> as prefix! */
            if (isInString(configLines[i], "IgnoreLeadingNaN") && isInString(configLines[i], ";")) { // It is the parameter "IgnoreLeadingNaN" and it is commented out
                migrated = migrated | replaceString(configLines[i], "true", "false"); // Set it to its default value
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }
        }

        if (section == "[MQTT]") {
            migrated = migrated | replaceString(configLines[i], ";Uri", "Uri"); // Enable it
            migrated = migrated | replaceString(configLines[i], ";MainTopic", "MainTopic"); // Enable it
            migrated = migrated | replaceString(configLines[i], ";ClientID", "ClientID"); // Enable it

            migrated = migrated | replaceString(configLines[i], "SetRetainFlag", "RetainMessages"); // First rename it, enable it with its default value
            migrated = migrated | replaceString(configLines[i], ";RetainMessages = true", ";RetainMessages = false"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";RetainMessages", "RetainMessages"); // Enable it

            migrated = migrated | replaceString(configLines[i], ";HomeassistantDiscovery = true", ";HomeassistantDiscovery = false"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";HomeassistantDiscovery", "HomeassistantDiscovery"); // Enable it

            if (isInString(configLines[i], "MeterType") && isInString(configLines[i], ";")) { // It is the parameter "MeterType" and it is commented out
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            if (configLines[i].rfind("Topic", 0) != std::string::npos)  // only if string starts with "Topic" (Was the naming in very old version)
            {
                migrated = migrated | replaceString(configLines[i], "Topic", "MainTopic");
            }
        }

        if (section == "[InfluxDB]") {
            if (isInString(configLines[i], "Uri") && isInString(configLines[i], ";")) { // It is the parameter "Uri" and it is commented out
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            if (isInString(configLines[i], "Database") && isInString(configLines[i], ";")) { // It is the parameter "Database" and it is commented out
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            /* Measurement has a <NUMBER> as prefix! */
            if (isInString(configLines[i], "Measurement") && isInString(configLines[i], ";")) { // It is the parameter "Measurement" and is it disabled
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            /* Fieldname has a <NUMBER> as prefix! */
            if (isInString(configLines[i], "Fieldname")) { // It is the parameter "Fieldname"
                migrated = migrated | replaceString(configLines[i], "Fieldname", "Field"); // Rename it to Field
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            /* Field has a <NUMBER> as prefix! */
            if (isInString(configLines[i], "Field") && isInString(configLines[i], ";")) { // It is the parameter "Field" and is it disabled
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }
        }

        if (section == "[InfluxDBv2]") {
            if (isInString(configLines[i], "Uri") && isInString(configLines[i], ";")) { // It is the parameter "Uri" and it is commented out
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            if (isInString(configLines[i], "Database") && isInString(configLines[i], ";")) { // It is the parameter "Database" and it is commented out
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            /* Measurement has a <NUMBER> as prefix! */
            if (isInString(configLines[i], "Measurement") && isInString(configLines[i], ";")) { // It is the parameter "Measurement" and is it disabled
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            /* Fieldname has a <NUMBER> as prefix! */
            if (isInString(configLines[i], "Fieldname")) { // It is the parameter "Fieldname"
                migrated = migrated | replaceString(configLines[i], "Fieldname", "Field"); // Rename it to Field
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            /* Field has a <NUMBER> as prefix! */
            if (isInString(configLines[i], "Field") && isInString(configLines[i], ";")) { // It is the parameter "Field" and is it disabled
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }
        }

        if (section == "[GPIO]") {

        }

        if (section == "[DataLogging]") {
            /* DataLogActive is true by default! */
            migrated = migrated | replaceString(configLines[i], ";DataLogActive = false", ";DataLogActive = true"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";DataLogActive", "DataLogActive"); // Enable it

            migrated = migrated | replaceString(configLines[i], "DataLogRetentionInDays", "DataFilesRetention");
        }

        if (section == "[AutoTimer]") {
            migrated = migrated | replaceString(configLines[i], ";AutoStart = true", ";AutoStart = false"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";AutoStart", "AutoStart"); // Enable it

            migrated = migrated | replaceString(configLines[i], "Intervall", "Interval");
        }

        if (section == "[Debug]") {
            migrated = migrated | replaceString(configLines[i], "Logfile ", "LogLevel "); // Whitespace needed so it does not match `LogfileRetentionInDays`
            /* LogLevel (resp. LogFile) was originally a boolean, but we switched it to an int
             * For both cases (true/false), we set it to level 2 (WARNING) */
            migrated = migrated | replaceString(configLines[i], "LogLevel = true", "LogLevel = 2");
            migrated = migrated | replaceString(configLines[i], "LogLevel = false", "LogLevel = 2");
            
            migrated = migrated | replaceString(configLines[i], "LogfileRetentionInDays", "LogfilesRetention");
        }

        if (section == "[System]") {
            if ((isInString(configLines[i], "TimeServer = undefined") || isInString(configLines[i], "TimeServer = pool.ntp.org")) && isInString(configLines[i], ";")) 
            { // It is the parameter "TimeServer" and is it disabled
                migrated = migrated | replaceString(configLines[i], "undefined", "pool.ntp.org");
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            if (isInString(configLines[i], "TimeZone") && isInString(configLines[i], ";")) { // It is the parameter "TimeZone" and it is commented out
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }

            if (isInString(configLines[i], "Hostname") && isInString(configLines[i], ";")) { // It is the parameter "Hostname" and is it disabled
                migrated = migrated | replaceString(configLines[i], "undefined", "watermeter");
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }
                                 
            migrated = migrated | replaceString(configLines[i], "RSSIThreashold", "RSSIThreshold");

            if (isInString(configLines[i], "CPUFrequency") && isInString(configLines[i], ";")) { // It is the parameter "CPUFrequency" and is it disabled
                migrated = migrated | replaceString(configLines[i], "240", "160");
                migrated = migrated | replaceString(configLines[i], ";", ""); // Enable it
            }
            
            migrated = migrated | replaceString(configLines[i], "AutoAdjustSummertime", ";UNUSED_PARAMETER"); // This parameter is no longer used

            migrated = migrated | replaceString(configLines[i], ";SetupMode = true", ";SetupMode = false"); // Set it to its default value
            migrated = migrated | replaceString(configLines[i], ";SetupMode", "SetupMode"); // Enable it
        }
    }

    if (migrated) { // At least one replacement happened
        if (! RenameFile(CONFIG_FILE, CONFIG_FILE_BACKUP)) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to create backup of config file");
            return;
        }

        FILE* pfile = fopen(CONFIG_FILE, "w");        
        for (int i = 0; i < configLines.size(); i++) {
            fwrite(configLines[i].c_str() , configLines[i].length(), 1, pfile);
            fwrite("\n" , 1, 1, pfile);
        }
        fclose(pfile);
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Config file migrated. Saved backup to " + string(CONFIG_FILE_BACKUP));
    }
}