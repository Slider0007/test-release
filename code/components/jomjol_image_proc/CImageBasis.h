#ifndef CIMAGEBASIS_H
#define CIMAGEBASIS_H

#include "../../include/defines.h"

#include <string>

#include <esp_http_server.h>

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "stb/stb_image_resize.h"


struct ImageData
{
    uint8_t data[MAX_JPG_SIZE];
    size_t size = 0;
};



class CImageBasis
{
    protected:
        bool externalImage;
        std::string filename;
        std::string name; // Just used for diagnostics
        int memsize = 0;

        void memCopy(uint8_t* _source, uint8_t* _target, int _size);
        bool isInImage(int x, int y);

        bool islocked;

    public:
        uint8_t* rgb_image = NULL;
        int channels;
        int width, height, bpp; 

        uint8_t * RGBImageLock(int _waitmaxsec = 60);
        void RGBImageRelease();
        uint8_t * RGBImageGet();

        int getWidth() {return this->width;};   
        int getHeight() {return this->height;};   
        int getChannels() {return this->channels;};   
        int getMemsize() {return this->memsize;};
        void drawRect(int x, int y, int dx, int dy, int r = 255, int g = 255, int b = 255, int thickness = 1);
        void drawLine(int x1, int y1, int x2, int y2, int r, int g, int b, int thickness = 1);
        void drawCircle(int x1, int y1, int rad, int r, int g, int b, int thickness = 1);
        void drawEllipse(int x1, int y1, int radx, int rady, int r, int g, int b, int thickness = 1);

        void setPixelColor(int x, int y, int r, int g, int b);
        void Contrast(float _contrast);
        bool ImageOkay();
        bool CopyFromMemory(uint8_t* _source, int _size);

        void SetIndepended(){externalImage = false;};

        bool CreateEmptyImage(int _width, int _height, int _channels);
        bool CreateEmptyImage(int _width, int _height, int _channels, int add);
        void EmptyImage();


        CImageBasis(std::string name);
        CImageBasis(std::string name, std::string _image);
        CImageBasis(std::string name, std::string _image, bool _externalImage);
        CImageBasis(std::string name, uint8_t* _rgb_image, int _channels, int _width, int _height, int _bpp);
        CImageBasis(std::string name, int _width, int _height, int _channels);
        CImageBasis(std::string name, CImageBasis *_copyfrom);
        CImageBasis(std::string _name, CImageBasis *_copyfrom, int add);

        void Resize(int _new_dx, int _new_dy);        
        void Resize(int _new_dx, int _new_dy, CImageBasis *_target);        

        void LoadFromMemory(stbi_uc *_buffer, int len);
        void LoadFromMemoryPreallocated(stbi_uc *_buffer, int len);
        void LoadFromFilePreallocated(std::string _name, std::string _image);

        ImageData* writeToMemoryAsJPG(const int quality = 90);
        void writeToMemoryAsJPG(ImageData* ii, const int quality = 90);

        esp_err_t SendJPGtoHTTP(httpd_req_t *req, const int quality = 90);   

        uint8_t GetPixelColor(int x, int y, int ch);

        virtual ~CImageBasis();

        void SaveToFile(std::string _imageout);
};


#endif //CIMAGEBASIS_H

