#include "ClassControllCamera.h"
#include "../../include/defines.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <nvs_flash.h>
#include <sys/param.h>
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_camera.h"

#include "psram.h"
#include "Helper.h"
#include "statusled.h"
#include "CImageBasis.h"
#include "ClassLogFile.h"
#include "server_ota.h"
#include "server_GPIO.h"
#include "MainFlowControl.h"


static const char *TAG = "CAM_CTRL"; 

CCamera Camera;

/* Camera live stream */
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";


static camera_config_t camera_config = {
    .pin_pwdn       = PWDN_GPIO_NUM,
    .pin_reset      = RESET_GPIO_NUM,
    .pin_xclk       = XCLK_GPIO_NUM,
    .pin_sccb_sda   = SIOD_GPIO_NUM,
    .pin_sccb_scl   = SIOC_GPIO_NUM,
    .pin_d7         = Y9_GPIO_NUM,
    .pin_d6         = Y8_GPIO_NUM,
    .pin_d5         = Y7_GPIO_NUM,
    .pin_d4         = Y6_GPIO_NUM,
    .pin_d3         = Y5_GPIO_NUM,
    .pin_d2         = Y4_GPIO_NUM,
    .pin_d1         = Y3_GPIO_NUM,
    .pin_d0         = Y2_GPIO_NUM,
    .pin_vsync      = VSYNC_GPIO_NUM,
    .pin_href       = HREF_GPIO_NUM,
    .pin_pclk       = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,           // Frequency (20Mhz)
    
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,     // YUV422, GRAYSCALE, RGB565, JPEG
    .frame_size = FRAMESIZE_VGA,        // QQVGA-UXGA Do not use sizes above QVGA when not JPEG
    .jpeg_quality = 12,                 // 0-63 lower number means higher quality
    .fb_count = 1,                      // if more than one, i2s runs in continuous mode. Use only with JPEG
    .fb_location = CAMERA_FB_IN_PSRAM,  // The location where the frame buffer will be allocated */
    .grab_mode = CAMERA_GRAB_LATEST     // only from new esp32cam version
};


void CCamera::ledc_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = { };

    ledc_timer.speed_mode       = LEDC_MODE;
    ledc_timer.timer_num        = LEDC_TIMER;
    ledc_timer.duty_resolution  = LEDC_DUTY_RES;
    ledc_timer.freq_hz          = LEDC_FREQUENCY;   // Set output frequency at 5 kHz
    ledc_timer.clk_cfg          = LEDC_AUTO_CLK;

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = { };

    ledc_channel.speed_mode     = LEDC_MODE;
    ledc_channel.channel        = LEDC_CHANNEL;
    ledc_channel.timer_sel      = LEDC_TIMER;
    ledc_channel.intr_type      = LEDC_INTR_DISABLE;
    ledc_channel.gpio_num       = LEDC_OUTPUT_IO;
    ledc_channel.duty           = 0; // Set duty to 0%
    ledc_channel.hpoint         = 0;

    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}


CCamera::CCamera()
{
    #ifdef DEBUG_DETAIL_ON    
        ESP_LOGD(TAG, "CreateClassCamera");
    #endif

    brightness = 0;
    contrast = 0;
    saturation = 0;
    isFixedExposure = false;
    flashduration = 5000;
    led_intensity = 4095;
    demoMode = false;
    CameraInitSuccessful = false;

    #ifdef GPIO_FLASHLIGHT_DEFAULT_USE_LEDC
        ledc_init();   
    #endif
}


void CCamera::PowerResetCamera()
{
    #if PWDN_GPIO_NUM != GPIO_NUM_NC
        ESP_LOGD(TAG, "Resetting camera by power cycling");
        gpio_config_t conf;
        conf.intr_type = GPIO_INTR_DISABLE;
        conf.pin_bit_mask = 1LL << PWDN_GPIO_NUM;
        conf.mode = GPIO_MODE_OUTPUT;
        conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&conf);

        // carefull, logic is inverted compared to reset pin
        gpio_set_level(PWDN_GPIO_NUM, 1);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        gpio_set_level(PWDN_GPIO_NUM, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return;
    #else
        ESP_LOGD(TAG, "Power pin not defined. Software power reset not available"); 
        return;
    #endif
}


esp_err_t CCamera::InitCam()
{
    if (CameraInitSuccessful)
        DeinitCam(); // De-init in case it was already initialized
    
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Camera init failed: " + intToHexString(err));

            if (err == ESP_ERR_NOT_FOUND)
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Camera module not found, check camera module and electrical connection");
            else if (err == ESP_ERR_NOT_SUPPORTED)
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Camera module or feature not supported");
        
        return err;
    }

    ActualQuality = camera_config.jpeg_quality;
    ActualResolution = camera_config.frame_size;

    CameraInitSuccessful = true;
    return ESP_OK;
}


esp_err_t CCamera::DeinitCam()
{
    CameraInitSuccessful = false;
    esp_camera_deinit(); // De-init in case it was already initialized (returns ESP_FAIL if deinit is already done)
    PowerResetCamera();

    return ESP_OK;
}


bool CCamera::testCamera(void) 
{
    bool success;
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        success = true;
    }
    else {
        success = false;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Camera framebuffer check failed");
    }  
    esp_camera_fb_return(fb);

    return success;
}


void CCamera::printCamInfo(void)
{
    // Print camera infos
    // ********************************************
    char caminfo[64];
    sensor_t * s = esp_camera_sensor_get();
    sprintf(caminfo, "PID: 0x%02x, VER: 0x%02x, MIDL: 0x%02x, MIDH: 0x%02x, FREQ: %dMhz", s->id.PID, 
                s->id.VER, s->id.MIDH, s->id.MIDL, s->xclk_freq_hz/1000000);
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Camera info: " + std::string(caminfo));
}


void CCamera::SetCameraFrequency(int _frequency)
{
    if (camera_config.xclk_freq_hz == (_frequency * 1000000)) // If frequency is matching, return without any action
        return;
    
    if (_frequency >= 8 && _frequency <= 20)
        camera_config.xclk_freq_hz = _frequency * 1000000;
    else
        camera_config.xclk_freq_hz = 2000000;

    InitCam();
    printCamInfo();
    
    ESP_LOGD(TAG, "Set camera frequency: %d", camera_config.xclk_freq_hz);
}


void CCamera::SetQualitySize(int qual, framesize_t resol)
{
    if (!getCameraInitSuccessful())
        return;
    
    qual = std::min(63, std::max(8, qual)); // Limit quality from 8..63 (values lower than 8 tent to be unstable)
    
    sensor_t * s = esp_camera_sensor_get();
    if (s) {
        s->set_quality(s, qual);    
        s->set_framesize(s, resol);
    }
    else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SetQualitySize: Failed to get control structure");
    }

    ActualResolution = resol;
    ActualQuality = qual;

    if (resol == FRAMESIZE_QVGA)
    {
        image_height = 240;
        image_width = 320;             
    }
    else if (resol == FRAMESIZE_VGA)
    {
        image_height = 480;
        image_width = 640;             
    }
}


bool CCamera::SetBrightnessContrastSaturation(int _brightness, int _contrast, int _saturation)
{
    if (!getCameraInitSuccessful())
        return false;
    
    _brightness = std::min(2, std::max(-2, _brightness));
    _contrast = std::min(2, std::max(-2, _contrast));
    _saturation = std::min(2, std::max(-2, _saturation));

    sensor_t * s = esp_camera_sensor_get();
    if (s) {
        s->set_saturation(s, _saturation);
        s->set_contrast(s, _contrast);
        s->set_brightness(s, _brightness);

        /* Workaround - bug in cam library - enable bits are set without using bitwise OR logic -> only latest enable setting is used */
        /* Library version: https://github.com/espressif/esp32-camera/commit/5c8349f4cf169c8a61283e0da9b8cff10994d3f3 */
        /* Reference: https://esp32.com/viewtopic.php?f=19&t=14376#p93178 */
        /* The memory structure is as follows for 
        byte_0 = enable_bits
            byte_0->bit0 = enable saturation and hue --> OK
            byte_0->bit1 = enable saturation --> OK
            byte_0->bit2 = enable brightness and contrast --> OK
            byte_0->bit3 = enable green -> blue spitial effect (Antique and blunish and greenish and readdish and b&w) enable
            byte_0->bit4 = anable gray -> read spitial effect (Antique and blunish and greenish and readdish and b&w) enable
            byte_0->bit5 = remove (UV) in YUV color system
            byte_0->bit6 = enable negative
            byte_0->bit7 = remove (Y) in YUV color system
        byte_1 = saturation1 0-255 --> ?
        byte_2 = hue 0-255 --> OK
        byte_3 = saturation2 0-255 --> OK
        byte_4 = reenter saturation2 in documents --> ?
        byte_5 = spital effect green -> blue 0-255 --> ?
        byte_6 = spital effect gray -> read 0-255 --> ?
        byte_7 = contrast lower byte 0-255 --> OK
        byte_8 = contrast higher byte 0-255 --> OK
        byte_9 = brightness 0-255 --> OK
        byte_10= if byte_10==4 contrast effective --> ?
        */

        //s->set_reg(s, 0x7C, 0xFF, 2); // Optional feature - hue setting: Select byte 2 in register 0x7C to set hue value
        //s->set_reg(s, 0x7D, 0xFF, 0); // Optional feature - hue setting: Hue value 0 - 255
        s->set_reg(s, 0xFF, 0x01, 0); // Select DSP bank
        s->set_reg(s, 0x7C, 0xFF, 0); // Select byte 0 in register 0x7C
        s->set_reg(s, 0x7D, 7, 7); // Set bit 0, 1, 2 in register 0x7D to enable saturation, contrast, brightness and hue control
    }
    else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SetBrightnessContrastSaturation: Failed to get control structure");
    }

    if (((_brightness != brightness) || (_contrast != contrast) || (_saturation != saturation)) && isFixedExposure)
        EnableAutoExposure(flashduration);

    brightness = _brightness;
    contrast = _contrast;
    saturation = _saturation;

    ESP_LOGD(TAG, "brightness %d, contrast: %d, saturation %d", brightness, contrast, saturation);

    return true;
}


void CCamera::SetLEDIntensity(int _intensity)
{
    _intensity = std::min(_intensity, 100);
    _intensity = std::max(_intensity, 0);
    led_intensity = ((_intensity * LEDC_RESOLUTION) / 100);
    ESP_LOGD(TAG, "Set led_intensity to %d of %d", led_intensity, LEDC_RESOLUTION); // TODO: LOGD
}


bool CCamera::EnableAutoExposure(int _flashduration)
{
    if (!getCameraInitSuccessful())
        return false;
    
    flashduration = _flashduration; // save last flashduration internally
    ESP_LOGD(TAG, "EnableAutoExposure");
    
    if (flashduration > 0) {
        LEDOnOff(true);
        LightOnOff(true);
        vTaskDelay(flashduration / portTICK_PERIOD_MS);
    }

    camera_fb_t * fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();

    if (flashduration > 0) {
        LEDOnOff(false);  
        LightOnOff(false);
    }    

    if (!fb) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "EnableAutoExposure: Capture Failed. "
                                                "Check camera module and/or proper electrical connection");
        //doReboot();
        return false;
    }
    esp_camera_fb_return(fb);

    sensor_t * s = esp_camera_sensor_get(); 
    if (s) {
        s->set_gain_ctrl(s, 0);
        s->set_exposure_ctrl(s, 0);
    }
    else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "EnableAutoExposure: Failed to get control structure to set gain+exposure");
    }

    isFixedExposure = true;
    return true;
}


esp_err_t CCamera::CaptureToBasisImage(CImageBasis *_Image, int _flashduration)
{
	#ifdef DEBUG_DETAIL_ON
	    LogFile.WriteHeapInfo("CaptureToBasisImage - Start");
	#endif

    if (!getCameraInitSuccessful())
        return ESP_FAIL;

    flashduration = _flashduration; // save last flashduration internally

    if (flashduration > 0) {    // Switch on for defined time if a flashduration is set
        LEDOnOff(true);
        LightOnOff(true);
        vTaskDelay(flashduration / portTICK_PERIOD_MS);
    }

	#ifdef DEBUG_DETAIL_ON
	    LogFile.WriteHeapInfo("CaptureToBasisImage - After LightOn");
	#endif

    camera_fb_t * fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);        
    fb = esp_camera_fb_get();

    if (flashduration > 0) {    // Switch off if flashlight was on
        LEDOnOff(false);  
        LightOnOff(false);
    }

    if (!fb) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: Failed to get camera framebuffer");
        return ESP_FAIL;
    }

    if (demoMode) { // Use images stored on SD-Card instead of camera image
        /* Replace Framebuffer with image from SD-Card */
        loadNextDemoImage(fb);
    }

    if (_Image != NULL) {
        STBIObjectPSRAM.name="rawImage";
        STBIObjectPSRAM.usePreallocated = true;
        STBIObjectPSRAM.PreallocatedMemory = _Image->RGBImageGet();
        STBIObjectPSRAM.PreallocatedMemorySize = _Image->getMemsize();

        _Image->LoadFromMemoryPreallocated(fb->buf, fb->len);
    }
    else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: rawImage not allocated");
    }
    esp_camera_fb_return(fb);        
     
    #ifdef DEBUG_DETAIL_ON
        LogFile.WriteHeapInfo("CaptureToBasisImage - Done");
    #endif

    return ESP_OK;    
}


esp_err_t CCamera::CaptureToFile(std::string nm, int _flashduration)
{
    if (!getCameraInitSuccessful())
        return ESP_FAIL;
    
    std::string ftype;
    flashduration = _flashduration; // save last flashduration internally

    if (flashduration > 0) {    // Switch on for defined time if a flashduration is set
        LEDOnOff(true);
        LightOnOff(true);
        vTaskDelay(flashduration / portTICK_PERIOD_MS);
    }

    camera_fb_t * fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();

    if (flashduration > 0) {    // Switch off if flashlight was on
        LEDOnOff(false);    
        LightOnOff(false);
    }

    if (!fb) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: Failed to get camera framebuffer");
        return ESP_FAIL;
    }

    #ifdef DEBUG_DETAIL_ON    
        ESP_LOGD(TAG, "w %d, h %d, size %d", fb->width, fb->height, fb->len);
    #endif

    nm = FormatFileName(nm);

    #ifdef DEBUG_DETAIL_ON
        ESP_LOGD(TAG, "Save Camera to: %s", nm.c_str());
    #endif

    ftype = toUpper(getFileType(nm));

    #ifdef DEBUG_DETAIL_ON
        ESP_LOGD(TAG, "Filetype: %s", ftype.c_str());
    #endif

    uint8_t * buf = NULL;
    size_t buf_len = 0;   
    bool converted = false; 

    if (ftype.compare("BMP") == 0)
    {
        frame2bmp(fb, &buf, &buf_len);
        converted = true;
    }
    if (ftype.compare("JPG") == 0)
    {
        if(fb->format != PIXFORMAT_JPEG){
            bool jpeg_converted = frame2jpg(fb, ActualQuality, &buf, &buf_len);
            converted = true;
            if(!jpeg_converted){
                ESP_LOGE(TAG, "JPEG compression failed");
            }
        } else {
            buf_len = fb->len;
            buf = fb->buf;
        }
    }

    esp_camera_fb_return(fb);

    FILE * fp = fopen(nm.c_str(), "wb");
    if (fp == NULL) { // If an error occurs during the file creation
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: Failed to open file " + nm);
    }
    else {
        /* Related to article: https://blog.drorgluska.com/2022/06/esp32-sd-card-optimization.html */
        // Set buffer to SD card allocation size of 512 byte (newlib default: 128 byte) -> reduce system read/write calls
        setvbuf(fp, NULL, _IOFBF, 512);

        fwrite(buf, sizeof(uint8_t), buf_len, fp); 
        fclose(fp);
    }   

    if (converted)
        free(buf);

    return ESP_OK;    
}


static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;

    if(!index) {
        j->len = 0;
    }

    if(httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
        return 0;
    }

    j->len += len;

    return len;
}


esp_err_t CCamera::CaptureToHTTP(httpd_req_t *req, int _flashduration)
{
    if (!getCameraInitSuccessful())
        return ESP_FAIL;
    
    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_start = esp_timer_get_time();
    flashduration = _flashduration; // save last flashduration internally

    if (flashduration > 0) {
        LEDOnOff(true);
        LightOnOff(true);
        vTaskDelay(flashduration / portTICK_PERIOD_MS);
    }

    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();

    if (flashduration > 0) {    // Switch off if flashlight was on
        LEDOnOff(false); 
        LightOnOff(false);
    }

    if (!fb) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: Failed to get camera framebuffer");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
  
    res = httpd_resp_set_type(req, "image/jpeg");
    if(res == ESP_OK) {
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=raw.jpg");
    }

    if(res == ESP_OK) {
        if (demoMode) { // Use images stored on SD-Card instead of camera image
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Demo mode active");
            /* Replace Framebuffer with image from SD-Card */
            loadNextDemoImage(fb);

            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
        }
        else {
            if(fb->format == PIXFORMAT_JPEG) {
                fb_len = fb->len;
                res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
            } 
            else {
                jpg_chunking_t jchunk = {req, 0};
                res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk)?ESP_OK:ESP_FAIL;
                httpd_resp_send_chunk(req, NULL, 0);
                fb_len = jchunk.len;
            }
        }
    }
    esp_camera_fb_return(fb);
    
    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "JPG: %dKB %dms", (int)(fb_len/1024), (int)((fr_end - fr_start)/1000));

    return res;
}


esp_err_t CCamera::CaptureToStream(httpd_req_t *req, bool FlashlightOn)
{
    if (!getCameraInitSuccessful())
        return ESP_FAIL;
    
    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_start;
    char * part_buf[64];

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Live stream started");

    if (FlashlightOn) {
        LEDOnOff(true);
        LightOnOff(true);
    }

    //httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");  //stream is blocking web interface, only serving to local

    httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

    while(1)
    {
        fr_start = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        esp_camera_fb_return(fb);
        fb = esp_camera_fb_get();
        if (!fb) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToStream: Failed to get camera framebuffer");
            break;
        }
        fb_len = fb->len;
   
        if (res == ESP_OK) {
            size_t hlen = snprintf((char *)part_buf, sizeof(part_buf), _STREAM_PART, fb_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb_len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        
        esp_camera_fb_return(fb);

        int64_t fr_end = esp_timer_get_time();
        ESP_LOGD(TAG, "JPG: %dKB %dms", (int)(fb_len/1024), (int)((fr_end - fr_start)/1000));

        if (res != ESP_OK){ // Exit loop, e.g. also when closing the webpage
            break;
        }

        int64_t fr_delta_ms = (fr_end - fr_start) / 1000;
        if (CAM_LIVESTREAM_REFRESHRATE > fr_delta_ms) {
            const TickType_t xDelay = (CAM_LIVESTREAM_REFRESHRATE - fr_delta_ms)  / portTICK_PERIOD_MS;
            ESP_LOGD(TAG, "Stream: sleep for: %ldms", (long) xDelay*10);
            vTaskDelay(xDelay);        
        }
    }

    LEDOnOff(false);
    LightOnOff(false);

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Live stream stopped");

    return res;
}


void CCamera::LightOnOff(bool status)
{
    GpioHandler* gpioHandler = gpio_handler_get();
    if ((gpioHandler != NULL) && (gpioHandler->isEnabled())) {
        ESP_LOGD(TAG, "GPIO handler enabled: Trigger flashlight by GPIO handler");
        gpioHandler->flashLightEnable(status);
    }
    else {
    #ifdef GPIO_FLASHLIGHT_DEFAULT_USE_LEDC
        if (status) {
            ESP_LOGD(TAG, "Default flashlight turn on with PWM %d", led_intensity);
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, led_intensity));
            // Update duty to apply the new value
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
        }
        else {
            ESP_LOGD(TAG, "Default flashlight turn off PWM");
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
        }
    #else
        // Init the GPIO
        esp_rom_gpio_pad_select_gpio(FLASH_GPIO);
        // Set the GPIO as a push/pull output 
        gpio_set_direction(GPIO_FLASHLIGHT_DEFAULT, GPIO_MODE_OUTPUT);  

        if (status)  
            gpio_set_level(GPIO_FLASHLIGHT_DEFAULT, 1);
        else
            gpio_set_level(GPIO_FLASHLIGHT_DEFAULT, 0);
    #endif
    }
}


void CCamera::LEDOnOff(bool status)
{
	if (xHandle_task_StatusLED == NULL) {
        // Init the GPIO
        esp_rom_gpio_pad_select_gpio(GPIO_STATUS_LED_ONBOARD);
        /* Set the GPIO as a push/pull output */
        gpio_set_direction(GPIO_STATUS_LED_ONBOARD, GPIO_MODE_OUTPUT);  

        if (!status)  
            gpio_set_level(GPIO_STATUS_LED_ONBOARD, 1);
        else
            gpio_set_level(GPIO_STATUS_LED_ONBOARD, 0);   
    }
}


void CCamera::GetCameraParameter(httpd_req_t *req, int &qual, framesize_t &resol)
{
    char _query[100];
    char _qual[10];
    char _size[10];

    resol = ActualResolution;
    qual = ActualQuality;


    if (httpd_req_get_url_query_str(req, _query, 100) == ESP_OK)
    {
        ESP_LOGD(TAG, "Query: %s", _query);
        if (httpd_query_key_value(_query, "size", _size, 10) == ESP_OK)
        {
            #ifdef DEBUG_DETAIL_ON   
            ESP_LOGD(TAG, "Size: %s", _size);
            #endif
            if (strcmp(_size, "QVGA") == 0)
                resol = FRAMESIZE_QVGA;       // 320x240
            else if (strcmp(_size, "VGA") == 0)
                resol = FRAMESIZE_VGA;      // 640x480
            else if (strcmp(_size, "SVGA") == 0)
                resol = FRAMESIZE_SVGA;     // 800x600
            else if (strcmp(_size, "XGA") == 0)
                resol = FRAMESIZE_XGA;      // 1024x768
            else if (strcmp(_size, "SXGA") == 0)
                resol = FRAMESIZE_SXGA;     // 1280x1024
            else if (strcmp(_size, "UXGA") == 0)
                 resol = FRAMESIZE_UXGA;     // 1600x1200   
        }
        if (httpd_query_key_value(_query, "quality", _qual, 10) == ESP_OK)
        {
            #ifdef DEBUG_DETAIL_ON   
            ESP_LOGD(TAG, "Quality: %s", _qual);
            #endif
            qual = atoi(_qual);
                
            if (qual > 63)      // Limit to max. 63
                qual = 63;
            else if (qual < 8)  // Limit to min. 8
                qual = 8;
        }
    }
}


framesize_t CCamera::TextToFramesize(const char * _size)
{
    if (strcmp(_size, "QVGA") == 0)
        return FRAMESIZE_QVGA;       // 320x240
    else if (strcmp(_size, "VGA") == 0)
        return FRAMESIZE_VGA;      // 640x480
    else if (strcmp(_size, "SVGA") == 0)
        return FRAMESIZE_SVGA;     // 800x600
    else if (strcmp(_size, "XGA") == 0)
        return FRAMESIZE_XGA;      // 1024x768
    else if (strcmp(_size, "SXGA") == 0)
        return FRAMESIZE_SXGA;     // 1280x1024
    else if (strcmp(_size, "UXGA") == 0)
        return FRAMESIZE_UXGA;     // 1600x1200  

    return ActualResolution;
}


bool CCamera::getCameraInitSuccessful() 
{
    return CameraInitSuccessful;
}


void CCamera::EnableDemoMode()
{
    demoFiles.clear();

    char line[50];

    FILE *fd = fopen("/sdcard/demo/files.txt", "r");
    if (!fd) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Can not start Demo mode, the folder '/sdcard/demo/' does not contain the needed files");
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "See details on https://jomjol.github.io/AI-on-the-edge-device-docs/Demo-Mode");
        return;
    }

    while (fgets(line, sizeof(line), fd) != NULL) {
        line[strlen(line) - 1] = '\0';
        demoFiles.push_back(line);
    }
    
    fclose(fd);

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Using demo images (" + std::to_string(demoFiles.size()) + 
            " files) instead of real camera image");

    for (auto file : demoFiles) {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, file);
    }

    demoMode = true;
}


void CCamera::DisableDemoMode()
{
    demoMode = false;
    demoFiles.clear();
}


bool CCamera::loadNextDemoImage(camera_fb_t *fb) {
    char filename[50];
    long fileSize;

    snprintf(filename, sizeof(filename), "/sdcard/demo/%s", demoFiles[getFlowCycleCounter() % demoFiles.size()].c_str());

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Using " + std::string(filename) + " as demo image");

    /* Inject saved image */

    fileSize = getFileSize(filename);
    if (fileSize > DEMO_IMAGE_SIZE) {
        char buf[100];
        snprintf(buf, sizeof(buf), "Demo image (%d bytes) is larger than provided buffer (%d bytes)",
                (int)fileSize, DEMO_IMAGE_SIZE);
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, std::string(buf));
        return false;
    }

    FILE * fp = fopen(filename, "rb");
    if (!fp) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read file: " + std::string(filename));
        return false;
    }

    /* Related to article: https://blog.drorgluska.com/2022/06/esp32-sd-card-optimization.html */
    // Set buffer to SD card allocation size of 512 byte (newlib default: 128 byte) -> reduce system read/write calls
    setvbuf(fp, NULL, _IOFBF, 512);

    fb->len = fread(fb->buf, 1, fileSize, fp);
    fclose(fp);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "read " + std::to_string(fb->len) + " bytes");

    return true;
}


/* Free only user allocated memory without deinit of cam driver */
void CCamera::FreeMemoryOnly()
{
    demoFiles.clear();
}


CCamera::~CCamera()
{
    DeinitCam();
    demoFiles.clear();
}