#ifndef CLASSCONTROLCAMERA_H
#define CLASSCONTROLCAMERA_H

#include <string>
#include <vector>

#include <esp_http_server.h>
#include "esp_camera.h"

#include "CImageBasis.h"


typedef struct {
        httpd_req_t *req;
        size_t len;
} jpg_chunking_t;


struct CameraParameter {
    framesize_t actualResolution;
    int actualQuality;
    int flashIntensity;
    int flashTime;
    int brightness, contrast, saturation, sharpness;
    int exposureControlMode, autoExposureLevel, manualExposureValue;
    int gainControlMode, manualGainValue;
    int specialEffect;
    bool mirrorImage;
    bool flipImage;
    int zoomMode, zoomOffsetX, zoomOffsetY;
};


class ClassControlCamera
{
    protected:
        bool cameraInitSuccessful;
        CameraParameter camParameter;

        bool demoMode;
        std::vector<std::string> demoFiles;

        void setStatusLed(bool status);
        bool loadNextDemoImage(camera_fb_t *_fb);

        void setCamWindow(sensor_t *_s, int _resolution, int _xOffset, int _yOffset, int _xLength, int _yLength);
        void setImageWidthHeightFromResolution(framesize_t resol);

    public:
        int image_height, image_width;

        ClassControlCamera();
        ~ClassControlCamera();
        void freeMemoryOnly();
        void powerResetCamera();
        esp_err_t initCam();
        esp_err_t deinitCam();
        bool testCamera(void);
        void printCamInfo(void);
        void printCamConfig(void);
        bool getcameraInitSuccessful();

        CameraParameter getCameraParameter();
        std::string getCamType(void);
        std::string getCamPID(void);
        std::string getCamVersion(void);
        int getCamFrequencyMhz(void);

        esp_err_t captureToBasisImage(CImageBasis *_Image);
        esp_err_t captureToFile(std::string _nm);
        esp_err_t captureToHTTP(httpd_req_t *_req);
        esp_err_t captureToStream(httpd_req_t *_req, bool _flashlightOn);

        #ifdef GPIO_FLASHLIGHT_DEFAULT_USE_PWM
        void ledcInitFlashlightDefault(void);
        #endif

        void setFlashIntensity(int _flashIntensity);
        void setFlashTime(int _flashTime);
        int getFlashTime();
        void setFlashlight(bool _status);

        void setCameraFrequency(int _frequency);
        void setSizeQuality(int _qual, framesize_t _resol, int _zoomMode, int _zoomOffsetX, int _zoomOffsetY);
        void setZoom(int _zoomMode, int _zoomOffsetX, int _zoomOffsetY);
        bool setImageManipulation(int _brightness, int _contrast, int _saturation, int _sharpness, int _exposureControlMode,
                                  int _autoExposureLevel, int _manualExposureValue, int _gainControlMode, int _manualGainValue,
                                  int _specialEffect, bool _mirror, bool _flip);
        bool setMirrorFlip(bool _mirror, bool _flip);

        framesize_t textToFramesize(const char * text);

        void enableDemoMode(void);
        void disableDemoMode(void);
};


extern ClassControlCamera cameraCtrl;

#endif