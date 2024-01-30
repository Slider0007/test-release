let config_gesamt = "";  // Parsed config.ini as string
let config_split = [];   // Config.ini splitted by lines
var param = {};          // Configuration parameter object
var category = {};       // Configuration category obejct
var NUMBERS = [];        // Number sequences
let REFERENCES = [];     // Alignment marker
let tflite_list = "";    // TFLite model files as tab separated list


async function getNUMBERSList()    // Legacy: Not in use anymore (was only used for graph.html)
{
    return new Promise(function (resolve, reject) {
        var url = getDomainname() + '/editflow?task=namenumbers';

        var xhttp = new XMLHttpRequest();
        xhttp.onreadystatechange = function() {
            if (this.readyState == 4) {
            if (this.status >= 200 && this.status < 300) {
                        var namenumberslist = xhttp.responseText;
                        namenumberslist = namenumberslist.split("\t");
                        return resolve(namenumberslist);

                }
                else {
                        firework.launch("Sequence names request failed (Response status: " + this.status + 
                                    "). Repeat action or check logs.", 'danger', 30000);
                        console.error("Sequence names request failed. Response status: " + this.status);
                        return reject("Sequence names request failed");
                }
            }
        };

        xhttp.timeout = 10000; // 10 seconds
        xhttp.open("GET", url, true);
        xhttp.send();
    });
}


async function getDataFileList()
{
    return new Promise(function (resolve, reject) {
        var url = getDomainname() + '/editflow?task=data';     

        var xhttp = new XMLHttpRequest();
        xhttp.onreadystatechange = function() {
            if (this.readyState == 4) {
                    if (this.status >= 200 && this.status < 300) {
                        var datalist = xhttp.responseText;
                        datalist = datalist.split("\t");
                        datalist.pop();
                        datalist.sort();
                        return resolve(datalist);
                    }
                    else {
                        firework.launch("Data files request failed (Response status: " + this.status + 
                                "). Repeat action or check logs.", 'danger', 30000);
                        console.error("Data files request failed. Response status: " + this.status);
                        return reject("Data files request failed");
                    }
            }
        };

        xhttp.timeout = 10000; // 10 seconds
        xhttp.open("GET", url, true);
        xhttp.send();
    });
}


async function fetchTFLITEList()
{
    return new Promise(function (resolve, reject) {
        var url = getDomainname() + '/editflow?task=tflite';

        var xhttp = new XMLHttpRequest();
        xhttp.onreadystatechange = function() {
            if (this.readyState == 4) {
                if (this.status >= 200 && this.status < 300) {
                        var response = xhttp.responseText;
                        response = response.split("\t").filter(element => element); // Split at tab position and remove empty elements
                        response.sort();  // Sort elements by name
                        tflite_list = response;  // Save to global variable
                        return resolve(tflite_list);
                }
                else {
                        firework.launch("TFLite files request failed (Response status: " + this.status + 
                                "). Repeat action or check logs.", 'danger', 30000);
                        console.error("TFLite files request failed. Response status: " + this.status);
                        return reject("TFLite files request failed");
                }
            }
        };

        xhttp.timeout = 10000; // 10 seconds
        xhttp.open("GET", url, true);
        xhttp.send();
    });
}


async function loadConfig()
{
     return new Promise(function (resolve, reject) {
          var url = getDomainname() + '/fileserver/config/config.ini';

          var xhttp = new XMLHttpRequest();
          xhttp.onreadystatechange = function() {
               if (this.readyState == 4) {
                    if (this.status >= 200 && this.status < 300) {
                         config_gesamt = xhttp.responseText;
                         return resolve(config_gesamt);
                    }
                    else {
                         firework.launch("Loading config.ini failed (Response status: " + this.status + 
                                        "). Repeat action or check logs.", 'danger', 30000);
                         console.error("Loading config.ini failed. Response status: " + this.status);
                         return reject("Loading config.ini failed");
                    }
               }
          };

          xhttp.timeout = 10000;  // 10 seconds
          xhttp.open("GET", url, true);
          xhttp.send();
     });
}


function getConfig()
{
     return config_gesamt;
}


function ParseConfig() {
     config_split = config_gesamt.split("\n");
     var aktline = 0;

     //param = new Object();
     //category = new Object(); 

     var catname = "TakeImage";
     category[catname] = new Object(); 
     category[catname]["enabled"] = true;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "RawImagesLocation", false, "/log/source");
     ParamAddSingleValueWithPreset(param, catname, "RawImagesRetention", false, "5");
     ParamAddSingleValueWithPreset(param, catname, "WaitBeforeTakingPicture", true, "2.0");
     ParamAddSingleValueWithPreset(param, catname, "CameraFrequency", true, "20");
     ParamAddSingleValueWithPreset(param, catname, "ImageQuality", true, "12");
     ParamAddSingleValueWithPreset(param, catname, "ImageSize", true, "VGA");
     ParamAddSingleValueWithPreset(param, catname, "LEDIntensity", true, "50");
     ParamAddSingleValueWithPreset(param, catname, "Brightness", true, "0");
     ParamAddSingleValueWithPreset(param, catname, "Contrast", true, "0");
     ParamAddSingleValueWithPreset(param, catname, "Saturation", true, "0");
     ParamAddSingleValueWithPreset(param, catname, "FixedExposure", true, "false");
     ParamAddSingleValueWithPreset(param, catname, "Demo", true, "false");    

     var catname = "Alignment";
     category[catname] = new Object(); 
     category[catname]["enabled"] = true;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "AlignmentAlgo", true, "Default");
     ParamAddSingleValueWithPreset(param, catname, "SearchFieldX", true, "20");
     ParamAddSingleValueWithPreset(param, catname, "SearchFieldY", true, "20");     
     ParamAddSingleValueWithPreset(param, catname, "FlipImageSize", true, "false");
     ParamAddSingleValueWithPreset(param, catname, "InitialRotate", true, "0.0");
     ParamAddSingleValueWithPreset(param, catname, "InitialMirror", true, "false");
     ParamAddSingleValueWithPreset(param, catname, "SaveDebugInfo", true, "false");

     var catname = "Digits";
     category[catname] = new Object(); 
     category[catname]["enabled"] = false;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddModelWithPreset(param, catname, "Model", true);
     ParamAddSingleValueWithPreset(param, catname, "CNNGoodThreshold", true, "0.0"); 
     ParamAddSingleValueWithPreset(param, catname, "ROIImagesLocation", false, "/log/digit");
     ParamAddSingleValueWithPreset(param, catname, "ROIImagesRetention", false, "5");

     var catname = "Analog";
     category[catname] = new Object(); 
     category[catname]["enabled"] = false;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddModelWithPreset(param, catname, "Model", true);
     ParamAddSingleValueWithPreset(param, catname, "ROIImagesLocation", false, "/log/analog");
     ParamAddSingleValueWithPreset(param, catname, "ROIImagesRetention", false, "5");

     var catname = "PostProcessing";
     category[catname] = new Object(); 
     category[catname]["enabled"] = true;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "FallbackValueUse", true, "true");
     ParamAddSingleValueWithPreset(param, catname, "FallbackValueAgeStartup", true, "720");
     ParamAddSingleValueWithPreset(param, catname, "CheckDigitIncreaseConsistency", true, "false");
     ParamAddValue(param, catname, "AllowNegativeRates", 1, true, "true");
     ParamAddValue(param, catname, "DecimalShift", 1, true, "0");
     ParamAddValue(param, catname, "AnalogDigitalTransitionStart", 1, true, "9.2");
     ParamAddValue(param, catname, "MaxRateType", 1, true, "RatePerMin");
     ParamAddValue(param, catname, "MaxRateValue", 1, true, "0.1");
     ParamAddValue(param, catname, "ExtendedResolution", 1, true, "false");
     ParamAddValue(param, catname, "IgnoreLeadingNaN", 1, true, "false");
     ParamAddSingleValueWithPreset(param, catname, "SaveDebugInfo", true, "false");

     var catname = "MQTT";
     category[catname] = new Object(); 
     category[catname]["enabled"] = false;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "Uri", true, "mqtt://IP-ADDRESS:1883");
     ParamAddSingleValueWithPreset(param, catname, "MainTopic", true, "watermeter");
     ParamAddSingleValueWithPreset(param, catname, "ClientID", true, "watermeter");
     ParamAddSingleValueWithPreset(param, catname, "user", false, "undefined");
     ParamAddSingleValueWithPreset(param, catname, "password", false, "undefined");
     ParamAddSingleValueWithPreset(param, catname, "TLSEncryption", false, "false");
     ParamAddSingleValueWithPreset(param, catname, "TLSCACert", true, "/config/certs/ca.crt");
     ParamAddSingleValueWithPreset(param, catname, "TLSClientCert", true, "/config/certs/client.crt");
     ParamAddSingleValueWithPreset(param, catname, "TLSClientKey", true, "/config/certs/client.key");
     ParamAddSingleValueWithPreset(param, catname, "RetainMessages", true, "false");
     ParamAddSingleValueWithPreset(param, catname, "HomeassistantDiscovery", true, "false");
     ParamAddSingleValueWithPreset(param, catname, "MeterType", true, "other");

     var catname = "InfluxDB";
     category[catname] = new Object(); 
     category[catname]["enabled"] = false;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "Uri", true, "http://IP-ADDRESS:PORT");
     ParamAddSingleValueWithPreset(param, catname, "Database", true, "undefined");
     ParamAddSingleValueWithPreset(param, catname, "user", false, "undefined");
     ParamAddSingleValueWithPreset(param, catname, "password", false, "undefined");
     ParamAddSingleValueWithPreset(param, catname, "TLSEncryption", false, "false");
     ParamAddSingleValueWithPreset(param, catname, "TLSCACert", true, "/config/certs/ca.crt");
     ParamAddSingleValueWithPreset(param, catname, "TLSClientCert", true, "/config/certs/client.crt");
     ParamAddSingleValueWithPreset(param, catname, "TLSClientKey", true, "/config/certs/client.key");
     ParamAddValue(param, catname, "Measurement", 1, true, "undefined");
     ParamAddValue(param, catname, "Field", 1, true, "undefined");

     var catname = "InfluxDBv2";
     category[catname] = new Object(); 
     category[catname]["enabled"] = false;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "Uri", true, "http://IP-ADDRESS:PORT");
     ParamAddSingleValueWithPreset(param, catname, "Bucket", true, "undefined");
     ParamAddSingleValueWithPreset(param, catname, "Org", false, "undefined");
     ParamAddSingleValueWithPreset(param, catname, "Token", false, "undefined");
     ParamAddSingleValueWithPreset(param, catname, "TLSEncryption", false, "false");
     ParamAddSingleValueWithPreset(param, catname, "TLSCACert", true, "/config/certs/ca.crt");
     ParamAddSingleValueWithPreset(param, catname, "TLSClientCert", true, "/config/certs/client.crt");
     ParamAddSingleValueWithPreset(param, catname, "TLSClientKey", true, "/config/certs/client.key");
     ParamAddValue(param, catname, "Measurement", 1, true, "undefined");
     ParamAddValue(param, catname, "Field", 1, true, "undefined");

     var catname = "GPIO";
     category[catname] = new Object(); 
     category[catname]["enabled"] = false;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddValue(param, catname, "IO0", 6, false, "", [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
     ParamAddValue(param, catname, "IO1", 6, false, "",  [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
     ParamAddValue(param, catname, "IO3", 6, false, "",  [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
     ParamAddValue(param, catname, "IO4", 6, false, "",  [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
     ParamAddValue(param, catname, "IO12", 6, false, "",  [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
     ParamAddValue(param, catname, "IO13", 6, false, "",  [null, null, /^[0-9]*$/, null, null, /^[a-zA-Z0-9_-]*$/]);
     ParamAddSingleValueWithPreset(param, catname, "LEDType", true, "WS2812");
     ParamAddSingleValueWithPreset(param, catname, "LEDNumbers", true, "2");
     ParamAddValue(param, catname, "LEDColor", 3);
     param[catname]["LEDColor"]["value1"] = "50";
     param[catname]["LEDColor"]["value2"] = "50";
     param[catname]["LEDColor"]["value3"] = "50";

     var catname = "AutoTimer";
     category[catname] = new Object(); 
     category[catname]["enabled"] = true;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "AutoStart", true, "true");
     ParamAddSingleValueWithPreset(param, catname, "Interval", true, "5.0");     

     var catname = "DataLogging";
     category[catname] = new Object(); 
     category[catname]["enabled"] = true;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "DataLogActive", true, "true");
     ParamAddSingleValueWithPreset(param, catname, "DataFilesRetention", true, "5");     

     var catname = "Debug";
     category[catname] = new Object(); 
     category[catname]["enabled"] = true;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "LogLevel", true, "2");
     ParamAddSingleValueWithPreset(param, catname, "LogfilesRetention", true, "5");
     ParamAddSingleValueWithPreset(param, catname, "DebugFilesRetention", true, "5");

     var catname = "System";
     category[catname] = new Object(); 
     category[catname]["enabled"] = true;
     category[catname]["found"] = false;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "TimeServer", true, "pool.ntp.org");   
     ParamAddSingleValueWithPreset(param, catname, "TimeZone", true, "CET-1CEST,M3.5.0,M10.5.0/3");
     ParamAddSingleValueWithPreset(param, catname, "Hostname", true, "watermeter");   
     ParamAddSingleValueWithPreset(param, catname, "RSSIThreshold", false, "-75");   
     ParamAddSingleValueWithPreset(param, catname, "CPUFrequency", true, "160");
     ParamAddSingleValueWithPreset(param, catname, "SetupMode", true, "true");

     var catname = "WebUI";
     category[catname] = new Object(); 
     category[catname]["enabled"] = true;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "OverviewAutoRefresh", true, "true");
     ParamAddSingleValueWithPreset(param, catname, "OverviewAutoRefreshTime", true, "10");
     ParamAddSingleValueWithPreset(param, catname, "DataGraphAutoRefresh", true, "false");
     ParamAddSingleValueWithPreset(param, catname, "DataGraphAutoRefreshTime", true, "60");
     

     while (aktline < config_split.length) {
          for (var cat in category) {
               if (typeof config_split[aktline] === 'undefined') {
                    aktline++;
                    continue;
               }

               zw = cat.toUpperCase();
               zw1 = "[" + zw + "]";
               zw2 = ";[" + zw + "]";
               
               if ((config_split[aktline].trim().toUpperCase() == zw1) || (config_split[aktline].trim().toUpperCase() == zw2)) {
                    if (config_split[aktline].trim().toUpperCase() == zw1) {
                         category[cat]["enabled"] = true;
                    }
                    category[cat]["found"] = true;
                    category[cat]["line"] = aktline;
                    aktline = ParseConfigParamAll(aktline, cat);
                    continue;
               }
          }
          
          aktline++;
     }


     // Make the downward compatiblity with DataLogging
     if (category["DataLogging"]["enabled"] == false)
          category["DataLogging"]["enabled"] = true

     if (param["DataLogging"]["DataLogActive"]["enabled"] == false && param["DataLogging"]["DataLogActive"]["value1"] == "")
     {
          param["DataLogging"]["DataLogActive"]["found"] = true;
          param["DataLogging"]["DataLogActive"]["enabled"] = true;
          param["DataLogging"]["DataLogActive"]["value1"] = "true";
     }

     if (param["DataLogging"]["DataFilesRetention"]["enabled"] == false && param["DataLogging"]["DataFilesRetention"]["value1"] == "")
     {
          param["DataLogging"]["DataFilesRetention"]["found"] = true;
          param["DataLogging"]["DataFilesRetention"]["enabled"] = true;
          param["DataLogging"]["DataFilesRetention"]["value1"] = "3";
     }
}


function ParseConfigReduced() {
     config_split = config_gesamt.split("\n");
     var aktline = 0;

     param = new Object();
     category = new Object(); 

     var catname = "TakeImage";
     category[catname] = new Object(); 
     category[catname]["enabled"] = true;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "ImageSize", true, "VGA");

     var catname = "Alignment";
     category[catname] = new Object(); 
     category[catname]["enabled"] = true;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "FlipImageSize", true, "false");

     var catname = "MQTT";
     category[catname] = new Object(); 
     category[catname]["enabled"] = false;
     category[catname]["found"] = true;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "HomeassistantDiscovery", true, "false");

     var catname = "WebUI";
     category[catname] = new Object(); 
     category[catname]["enabled"] = true;
     category[catname]["found"] = false;
     param[catname] = new Object();
     ParamAddSingleValueWithPreset(param, catname, "OverviewAutoRefresh", true, "true");
     ParamAddSingleValueWithPreset(param, catname, "OverviewAutoRefreshTime", true, "10");
     ParamAddSingleValueWithPreset(param, catname, "DataGraphAutoRefresh", true, "false");
     ParamAddSingleValueWithPreset(param, catname, "DataGraphAutoRefreshTime", true, "60");


     while (aktline < config_split.length) {
          for (var cat in category) {
               if (typeof config_split[aktline] === 'undefined') {
                    aktline++;
                    continue;
               }
               
               zw = cat.toUpperCase();
               zw1 = "[" + zw + "]";
               zw2 = ";[" + zw + "]";

               if ((config_split[aktline].trim().toUpperCase() == zw1) || (config_split[aktline].trim().toUpperCase() == zw2)) {
                    if (config_split[aktline].trim().toUpperCase() == zw1) {
                         category[cat]["enabled"] = true;
                    }
                    category[cat]["found"] = true;
                    category[cat]["line"] = aktline;
                    aktline = ParseConfigParamAll(aktline, cat);
                    continue;
               }
          }
          aktline++;
     }
}


function ParamAddValue(param, _cat, _param, _anzParam = 1, _isNUMBER = false, _defaultValue = "", _checkRegExList = null)
{
     param[_cat][_param] = new Object(); 
     param[_cat][_param]["found"] = false;
     param[_cat][_param]["enabled"] = false;
     param[_cat][_param]["line"] = -1; 
     param[_cat][_param]["anzParam"] = _anzParam;
     param[_cat][_param]["defaultValue"] = _defaultValue;   // Parameter only used for numbers sequences
     param[_cat][_param]["Numbers"] = _isNUMBER;
     param[_cat][_param].checkRegExList = _checkRegExList;
}


/* Add a standalone single parameter (no parameter which is used in a number sequence) and set to default value */
function ParamAddSingleValueWithPreset(param, _cat, _param, _enabled, _value)
{
     if (param[_cat][_param] == null) {
          param[_cat][_param] = new Object();
          param[_cat][_param]["found"] = true;
          param[_cat][_param]["enabled"] = _enabled;
          param[_cat][_param]["value1"] = _value;
          param[_cat][_param]["line"] = -1; 
          param[_cat][_param]["anzParam"] = 1;
          param[_cat][_param]["defaultValue"] = "";   // Parameter only used for numbers sequences
          param[_cat][_param]["Numbers"] = false;
          param[_cat][_param].checkRegExList = null;
     }
}


/* Add a model parameter (no parameter which is used in a number sequence) and set to default value */
function ParamAddModelWithPreset(param, _cat, _param, _enabled)
{
     if (param[_cat][_param] == null) {
          param[_cat][_param] = new Object();
          param[_cat][_param]["found"] = true;
          param[_cat][_param]["enabled"] = _enabled;
          param[_cat][_param]["line"] = -1; 
          param[_cat][_param]["anzParam"] = 1;
          param[_cat][_param]["defaultValue"] = "";   // Parameter only used for number sequences
          param[_cat][_param]["Numbers"] = false;
          param[_cat][_param].checkRegExList = null;

          if (_cat == "Digits")
               filter = "/dig";
          else if (_cat == "Analog")
               filter = "/ana";
          

          for (var i = 0; i < tflite_list.length; ++i) {
               if (tflite_list[i].includes(filter)) {
                    param[_cat][_param]["value1"] = tflite_list[i]; // Set first occurence as default value to ensure at least one is set
                    break;
               }
          }
     }
     else if (param[_cat][_param]["value1"] == "") { // If value empty, ensure at least one model is selected to avoid crashes
          if (_cat == "Digits")
               filter = "/dig";
          else if (_cat == "Analog")
               filter = "/ana";

          for (var i = 0; i < tflite_list.length; ++i) {
               if (tflite_list[i].includes(filter)) {
                    param[_cat][_param]["value1"] = tflite_list[i]; // Set first occurence as default value to ensure at least one is set
                    break;
               }
          }
     }
}


function ParseConfigParamAll(_aktline, _catname){
     ++_aktline;

     while ((_aktline < config_split.length) 
            && !(config_split[_aktline][0] == "[") 
            && !((config_split[_aktline][0] == ";") && (config_split[_aktline][1] == "["))) {
          var _input = config_split[_aktline];
          let [isCom, input] = isCommented(_input);
          var linesplit = ZerlegeZeile(input);
          ParamExtractValueAll(param, linesplit, _catname, _aktline, isCom);
          if (!isCom && (linesplit.length >= 5) && (_catname == 'Digits'))
               ExtractROIs(input, "digit");
          if (!isCom && (linesplit.length >= 5) && (_catname == 'Analog'))
               ExtractROIs(input, "analog");
          if (!isCom && (linesplit.length == 3) && (_catname == 'Alignment'))
          {
               _newref = new Object();
               _newref["name"] = linesplit[0];
               _newref["x"] = linesplit[1];
               _newref["y"] = linesplit[2];
               REFERENCES.push(_newref);
          }

          ++_aktline;
     }    
     return _aktline; 
}

function ParamExtractValue(_param, _linesplit, _catname, _paramname, _aktline, _iscom, _anzvalue = 1){
     if ((_linesplit[0].toUpperCase() == _paramname.toUpperCase()) && (_linesplit.length > _anzvalue))
     {
          _param[_catname][_paramname]["found"] = true;
          _param[_catname][_paramname]["enabled"] = !_iscom;
          _param[_catname][_paramname]["line"] = _aktline;
          _param[_catname][_paramname]["anzpara"] = _anzvalue;
          for (var j = 1; j <= _anzvalue; ++j) {
               _param[_catname][_paramname]["value"+j] = _linesplit[j];
               }
     }
}

function ParamExtractValueAll(_param, _linesplit, _catname, _aktline, _iscom){
     for (var paramname in _param[_catname]) {
          _AktROI = "default";
          _AktPara = _linesplit[0];
          _pospunkt = _AktPara.indexOf (".");
          if (_pospunkt > -1)
          {
               _AktROI = _AktPara.substring(0, _pospunkt);
               _AktPara = _AktPara.substring(_pospunkt+1);
          }
          if (_AktPara.toUpperCase() == paramname.toUpperCase())
          {
               while (_linesplit.length <= _param[_catname][paramname]["anzParam"]) {
                    _linesplit.push("");
               }

               _param[_catname][paramname]["found"] = true;
               _param[_catname][paramname]["enabled"] = !_iscom;
               _param[_catname][paramname]["line"] = _aktline;
               if (_param[_catname][paramname]["Numbers"] == true)         // möglicher Multiusage
               {
                    abc = getNUMBERS(_linesplit[0]);
                    abc[_catname][paramname] = new Object;
                    abc[_catname][paramname]["found"] = true;
                    abc[_catname][paramname]["enabled"] = !_iscom;
     
                    for (var j = 1; j <= _param[_catname][paramname]["anzParam"]; ++j) {
                         abc[_catname][paramname]["value"+j] = _linesplit[j];
                         }
                    if (abc["name"] == "default")
                    {
                    for (_num in NUMBERS)         // wert mit Default belegen
                         {
                              if (NUMBERS[_num][_catname][paramname]["found"] == false)
                              {
                                   NUMBERS[_num][_catname][paramname]["found"] = true;
                                   NUMBERS[_num][_catname][paramname]["enabled"] = !_iscom;
                                   NUMBERS[_num][_catname][paramname]["line"] = _aktline;
                                   for (var j = 1; j <= _param[_catname][paramname]["anzParam"]; ++j) {
                                        NUMBERS[_num][_catname][paramname]["value"+j] = _linesplit[j];
                                        }

                              }
                         }
                    }
               }
               else
               {
                    _param[_catname][paramname]["found"] = true;
                    _param[_catname][paramname]["enabled"] = !_iscom;
                    _param[_catname][paramname]["line"] = _aktline;
                         for (var j = 1; j <= _param[_catname][paramname]["anzParam"]; ++j) {
                         _param[_catname][paramname]["value"+j] = _linesplit[j];
                         }
               }
          }
     }
}


function WriteConfigININew()
{
     // Cleanup empty NUMBERS
     for (var j = 0; j < NUMBERS.length; ++j)
     {
          if ((NUMBERS[j]["digit"].length + NUMBERS[j]["analog"].length) == 0)
          {
               NUMBERS.splice(j, 1);
          }
     }



     config_split = new Array(0);

     for (var cat in param) {
          text = "[" + cat + "]";
          if (!category[cat]["enabled"]) {
               text = ";" + text;
          }
          config_split.push(text);

          for (var name in param[cat]) {
               if (param[cat][name]["Numbers"])
               {
                    for (_num in NUMBERS)
                    {
                         text = NUMBERS[_num]["name"] + "." + name;

                         var text = text + " =" 
                         
                         for (var j = 1; j <= param[cat][name]["anzParam"]; ++j) {
                              if (!(typeof NUMBERS[_num][cat][name]["value"+j] == 'undefined'))
                                   text = text + " " + NUMBERS[_num][cat][name]["value"+j];
                              }
                         if (!NUMBERS[_num][cat][name]["enabled"]) {
                              text = ";" + text;
                         }
                         config_split.push(text);
                    }
               }
               else
               {
                    var text = name + " =" 
                    
                    for (var j = 1; j <= param[cat][name]["anzParam"]; ++j) {
                         if (!(typeof param[cat][name]["value"+j] == 'undefined'))
                              text = text + " " + param[cat][name]["value"+j];
                         }
                    if (!param[cat][name]["enabled"]) {
                         text = ";" + text;
                    }
                    config_split.push(text);
               }
          }
          if (cat == "Digits")
          {
               for (var _roi in NUMBERS)
               {
                    if (NUMBERS[_roi]["digit"].length > 0)
                    {
                         for (var _roiddet in NUMBERS[_roi]["digit"])
                         {
                              text = NUMBERS[_roi]["name"] + "." + NUMBERS[_roi]["digit"][_roiddet]["name"];
                              text = text + " " + NUMBERS[_roi]["digit"][_roiddet]["x"];
                              text = text + " " + NUMBERS[_roi]["digit"][_roiddet]["y"];
                              text = text + " " + NUMBERS[_roi]["digit"][_roiddet]["dx"];
                              text = text + " " + NUMBERS[_roi]["digit"][_roiddet]["dy"];
                              text = text + " " + NUMBERS[_roi]["digit"][_roiddet]["CCW"];
                              config_split.push(text);
                         }
                    }
               }
          }
          if (cat == "Analog")
          {
               for (var _roi in NUMBERS)
               {
                    if (NUMBERS[_roi]["analog"].length > 0)
                    {
                         for (var _roiddet in NUMBERS[_roi]["analog"])
                         {
                              text = NUMBERS[_roi]["name"] + "." + NUMBERS[_roi]["analog"][_roiddet]["name"];
                              text = text + " " + NUMBERS[_roi]["analog"][_roiddet]["x"];
                              text = text + " " + NUMBERS[_roi]["analog"][_roiddet]["y"];
                              text = text + " " + NUMBERS[_roi]["analog"][_roiddet]["dx"];
                              text = text + " " + NUMBERS[_roi]["analog"][_roiddet]["dy"];
                              text = text + " " + NUMBERS[_roi]["analog"][_roiddet]["CCW"];
                              config_split.push(text);
                         }
                    }
               }
          }
          if (cat == "Alignment")
          {
               for (var _roi in REFERENCES)
               {
                    text = REFERENCES[_roi]["name"];
                    text = text + " " + REFERENCES[_roi]["x"];
                    text = text + " " + REFERENCES[_roi]["y"];
                    config_split.push(text);
               }
          }

          config_split.push("");
     }
}



function isCommented(input)
     {
          let isComment = false;
          if (input.charAt(0) == ';') {
               isComment = true;
               input = input.substr(1, input.length-1);
          };
          return [isComment, input];
     }    


function getConfigCategory() {
     return category;
}


function getConfigParameters() {
     return param;
}


function ExtractROIs(_aktline, _type){
     var linesplit = ZerlegeZeile(_aktline);
     abc = getNUMBERS(linesplit[0], _type);
     abc["pos_ref"] = _aktline;
     abc["x"] = linesplit[1];
     abc["y"] = linesplit[2];
     abc["dx"] = linesplit[3];
     abc["dy"] = linesplit[4];
     abc["ar"] = parseFloat(linesplit[3]) / parseFloat(linesplit[4]);
     abc["CCW"] = "false";
     if (linesplit.length >= 6)
          abc["CCW"] = linesplit[5];
}


function getNUMBERS(_name, _type, _create = true)
{
     _pospunkt = _name.indexOf (".");
     if (_pospunkt > -1)
     {
          _digit = _name.substring(0, _pospunkt);
          _roi = _name.substring(_pospunkt+1);
     }
     else
     {
          _digit = "default";
          _roi = _name;
     }

     _ret = -1;

     for (i = 0; i < NUMBERS.length; ++i)
     {
          if (NUMBERS[i]["name"] == _digit)
               _ret = NUMBERS[i];
     }

     if (!_create)         // nicht gefunden und soll auch nicht erzeugt werden, ggf. geht eine NULL zurück
          return _ret;

     if (_ret == -1)
     {
          _ret = new Object();
          _ret["name"] = _digit;
          _ret['digit'] = new Array();
          _ret['analog'] = new Array();

          for (_cat in param)
               for (_param in param[_cat])
                    if (param[_cat][_param]["Numbers"] == true){
                         if (typeof  _ret[_cat] == 'undefined')
                              _ret[_cat] = new Object();
                         _ret[_cat][_param] = new Object();
                         _ret[_cat][_param]["found"] = false;
                         _ret[_cat][_param]["enabled"] = false;
                         _ret[_cat][_param]["anzParam"] = param[_cat][_param]["anzParam"]; 

                    }

          NUMBERS.push(_ret);
     }

     if (typeof _type == 'undefined')             // muss schon existieren !!! - also erst nach Digits / Analog aufrufen
          return _ret;

     neuroi = new Object();
     neuroi["name"] = _roi;
     _ret[_type].push(neuroi);


     return neuroi;

}


function getAlignmentMarker(){
     return REFERENCES;
}


function getNumberSequences(){
     return NUMBERS;
}


function CreateNumberSequence(_sequence_name){
     found = false;
     for (i = 0; i < NUMBERS.length; ++i) {
          if (NUMBERS[i]["name"] == _sequence_name)
               found = true;
     }

     if (found)
          return "Number sequence name is already existing, please choose another name";

     _ret = new Object();
     _ret["name"] = _sequence_name;
     _ret['digit'] = new Array();
     _ret['analog'] = new Array();

     for (_cat in param)
          for (_param in param[_cat])
               if (param[_cat][_param]["Numbers"] == true)
               {
                    if (typeof (_ret[_cat]) === "undefined")
                    {
                         _ret[_cat] = new Object();
                    }
                    _ret[_cat][_param] = new Object();
                    if (param[_cat][_param]["defaultValue"] === "")
                    {
                         _ret[_cat][_param]["found"] = false;
                         _ret[_cat][_param]["enabled"] = false;
                    }
                    else
                    {
                         _ret[_cat][_param]["found"] = true;
                         _ret[_cat][_param]["enabled"] = true;
                         _ret[_cat][_param]["value1"] = param[_cat][_param]["defaultValue"];

                    }
                    _ret[_cat][_param]["anzParam"] = param[_cat][_param]["anzParam"]; 

               }

     NUMBERS.push(_ret);               
     return "";
}


function RenameNumberSequence(_sequence_name_old, _sequence_name_new){
     if ((_sequence_name_new.indexOf(".") >= 0) || (_sequence_name_new.indexOf(",") >= 0) || 
         (_sequence_name_new.indexOf(" ") >= 0) || (_sequence_name_new.indexOf("\"") >= 0))
     {
          return "Number sequence name must not contain , . \" or a space";
     }

     index = -1;
     found = false;
     for (i = 0; i < NUMBERS.length; ++i) {
          if (NUMBERS[i]["name"] == _sequence_name_old)
               index = i;
          if (NUMBERS[i]["name"] == _sequence_name_new)
               found = true;
     }

     if (found)
          return "Number sequence name is already existing, please choose another name";

     NUMBERS[index]["name"] = _sequence_name_new;
     
     return "";
}


function DeleteNumberSequence(_sequence_name){
     if (NUMBERS.length == 1)
          return "One number sequence is mandatory. Therefore this cannot be deleted"
     

     index = -1;
     for (i = 0; i < NUMBERS.length; ++i) {
          if (NUMBERS[i]["name"] == _sequence_name)
               index = i;
     }

     if (index > -1) {
          NUMBERS.splice(index, 1);
     }

     return "";
}


function getROIInfo(_typeROI, _number){
     index = -1;
     for (var i = 0; i < NUMBERS.length; ++i)
          if (NUMBERS[i]["name"] == _number)
               index = i;

     if (index != -1)
          return NUMBERS[index][_typeROI];
     else
          return "";     
}


function CreateROI(_sequence_name, _type, _pos, _roinew, _x, _y, _dx, _dy, _CCW){
     _indexnumber = -1;
     for (j = 0; j < NUMBERS.length; ++j)
          if (NUMBERS[j]["name"] == _sequence_name)
               _indexnumber = j;

     if (_indexnumber == -1)
          return "Number sequence not existing. ROI cannot be created"

     found = false;
     for (i = 0; i < NUMBERS[_indexnumber][_type].length; ++i) {
          if (NUMBERS[_indexnumber][_type][i]["name"] == _roinew)
               found = true;
     }

     if (found)
          return "ROI name is already existing";

     _ret = new Object();
     _ret["name"] = _roinew;
     _ret["x"] = _x;
     _ret["y"] = _y;
     _ret["dx"] = _dx;
     _ret["dy"] = _dy;
     _ret["ar"] = _dx / _dy;
     _ret["CCW"] = _CCW;

     NUMBERS[_indexnumber][_type].splice(_pos+1, 0, _ret);

     return "";
}


function RenameROI(_sequence_name, _type, _roi_name_old, _roi_name_new){
     if ((_roi_name_new.includes("=")) || (_roi_name_new.includes(".")) || (_roi_name_new.includes(":")) ||
         (_roi_name_new.includes(",")) || (_roi_name_new.includes(";")) || (_roi_name_new.includes(" ")) || 
         (_roi_name_new.includes("\""))) 
     {
          return "ROI name must not contain . : , ; = \" or space";
     }

     index = -1;
     found = false;
     _indexnumber = -1;
     for (j = 0; j < NUMBERS.length; ++j)
          if (NUMBERS[j]["name"] == _sequence_name)
               _indexnumber = j;

     if (_indexnumber == -1)
          return "Number sequence not existing. ROI cannot be renamed"  

     for (i = 0; i < NUMBERS[_indexnumber][_type].length; ++i) {
          if (NUMBERS[_indexnumber][_type][i]["name"] == _roi_name_old)
               index = i;
          if (NUMBERS[_indexnumber][_type][i]["name"] == _roi_name_new)
               found = true;
     }

     if (found)
          return "ROI name is already existing";

     NUMBERS[_indexnumber][_type][index]["name"] = _roi_name_new;
     
     return "";
}