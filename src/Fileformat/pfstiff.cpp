/**
 * @brief Tiff facilities
 *
 * This file is a part of LuminanceHDR package.
 * ----------------------------------------------------------------------
 * Copyright (C) 2003,2004 Rafal Mantiuk and Grzegorz Krawczyk
 * Copyright (C) 2006 Giuseppe Rota
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * ----------------------------------------------------------------------
 *
 * @author Grzegorz Krawczyk, <krawczyk@mpi-sb.mpg.de>
 * slightly modified by Giuseppe Rota <grota@sourceforge.net> for Luminance HDR
 * added color management support by Franco Comida <fcomida@sourceforge.net>
 */

#include "pfstiff.h"

#include <QObject>
#include <QSysInfo>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QScopedPointer>

#include <cmath>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <cassert>
#include "Common/ResourceHandlerLcms.h"

#include "Libpfs/frame.h"
#include "Libpfs/domio.h"

#include "Common/LuminanceOptions.h"

namespace
{

///////////////////////////////////////////////////////////////////////////////////
// \brief This code is taken from tifficc.c from libcms distribution and sligthly modified
// \ref http://svn.ghostscript.com/ghostscript/trunk/gs/lcms2/utils/tificc/tificc.c
cmsHPROFILE
GetTIFFProfile(TIFF* in)
{
    cmsHPROFILE hProfile;
    void* iccProfilePtr;
    cmsUInt32Number iccProfileSize;

    if (TIFFGetField(in, TIFFTAG_ICCPROFILE, &iccProfileSize, &iccProfilePtr))
    {
        qDebug () << "iccProfileSize: " << iccProfileSize;
        hProfile = cmsOpenProfileFromMem(iccProfilePtr, iccProfileSize);

        if (hProfile) return hProfile;
    }

    // Try to see if "colorimetric" tiff
    cmsCIExyYTRIPLE primaries;
    cmsCIExyY whitePoint;
    cmsToneCurve* curve[3];

    cmsFloat32Number* chr;
    if (TIFFGetField(in, TIFFTAG_PRIMARYCHROMATICITIES, &chr))
    {
        primaries.Red.x     = chr[0];
        primaries.Red.y     = chr[1];
        primaries.Green.x   = chr[2];
        primaries.Green.y   = chr[3];
        primaries.Blue.x    = chr[4];
        primaries.Blue.y    = chr[5];

        primaries.Red.Y = primaries.Green.Y = primaries.Blue.Y = 1.0;

        cmsFloat32Number* wp;
        if (TIFFGetField (in, TIFFTAG_WHITEPOINT, &wp))
        {
            whitePoint.x = wp[0];
            whitePoint.y = wp[1];
            whitePoint.Y = 1.0;

            // Transferfunction is a bit harder....
            cmsUInt16Number *gmr;
            cmsUInt16Number *gmg;
            cmsUInt16Number *gmb;

            TIFFGetFieldDefaulted(in, TIFFTAG_TRANSFERFUNCTION, &gmr, &gmg, &gmb);

            curve[0] = cmsBuildTabulatedToneCurve16(NULL, 256, gmr);
            curve[1] = cmsBuildTabulatedToneCurve16(NULL, 256, gmg);
            curve[2] = cmsBuildTabulatedToneCurve16(NULL, 256, gmb);

            hProfile = cmsCreateRGBProfile (&whitePoint, &primaries, curve);

            cmsFreeToneCurve(curve[0]);
            cmsFreeToneCurve(curve[1]);
            cmsFreeToneCurve(curve[2]);

            return hProfile;
        }
    }

    return NULL;
}
// End of code form tifficc.c
///////////////////////////////////////////////////////////////////////////////

void
transform_to_rgb(unsigned char *ScanLineIn, unsigned char *ScanLineOut, uint32 size, int nSamples)
{
  for (uint32 i = 0; i < size; i += nSamples)
    {
      unsigned char C = *(ScanLineIn + i + 0);
      unsigned char M = *(ScanLineIn + i + 1);
      unsigned char Y = *(ScanLineIn + i + 2);
      unsigned char K = *(ScanLineIn + i + 3);
      *(ScanLineOut + i + 0) = ((255 - C) * (255 - K)) / 255;
      *(ScanLineOut + i + 1) = ((255 - M) * (255 - K)) / 255;
      *(ScanLineOut + i + 2) = ((255 - Y) * (255 - K)) / 255;
      *(ScanLineOut + i + 3) = 255;
    }
}

void
transform_to_rgb_16(uint16 *ScanLineIn, uint16 *ScanLineOut, uint32 size, int nSamples)
{
  for (uint32 i = 0; i < size/2; i += nSamples)
    {
      uint16 C = *(ScanLineIn + i + 0);
      uint16 M = *(ScanLineIn + i + 1);
      uint16 Y = *(ScanLineIn + i + 2);
      uint16 K = *(ScanLineIn + i + 3);
      *(ScanLineOut + i + 0) = ((65535 - C) * (65535 - K)) / 65535;
      *(ScanLineOut + i + 1) = ((65535 - M) * (65535 - K)) / 65535;
      *(ScanLineOut + i + 2) = ((65535 - Y) * (65535 - K)) / 65535;
      *(ScanLineOut + i + 3) = 65535;
    }
}

const float DIV_255 = 1.f/255.f;
const float DIV_256 = 1.f/256.f;
}

TiffReader::TiffReader(const char *filename, const char *tempfilespath, bool wod):
    tif( TIFFOpen(filename, "r") ),
    writeOnDisk(wod),
    fileName(filename),
    tempFilesPath(tempfilespath)
{
    if (!tif)
        throw std::runtime_error ("TIFF: could not open file for reading.");
    // read header containing width and height from file
    //--- image size
    TIFFGetField (tif.data(), TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField (tif.data(), TIFFTAG_IMAGELENGTH, &height);

    if (width * height <= 0)
    {
        throw std::runtime_error ("TIFF: illegal image size.");
    }

    //--- image parameters
    uint16 planar;
    TIFFGetField(tif.data(), TIFFTAG_PLANARCONFIG, &planar);
    qDebug() << "Planar configuration: " << planar;
    if (planar != PLANARCONFIG_CONTIG)
    {
        throw std::runtime_error ("TIFF: unsupported planar configuration");
    }

    if (!TIFFGetField(tif.data(), TIFFTAG_COMPRESSION, &comp))	// compression type
        comp = COMPRESSION_NONE;

    // type of photometric data
    if (!TIFFGetFieldDefaulted (tif.data(), TIFFTAG_PHOTOMETRIC, &phot))
    {
        throw std::runtime_error ("TIFF: unspecified photometric type");
    }

    qDebug () << "Photometric type : " << phot;

    uint16 *extra_sample_types = 0;
    uint16 extra_samples_per_pixel = 0;
    switch (phot)
    {
    case PHOTOMETRIC_LOGLUV:
    {
        qDebug ("Photometric data: LogLuv");
        if (comp != COMPRESSION_SGILOG && comp != COMPRESSION_SGILOG24)
        {
            throw std::runtime_error ("TIFF: only support SGILOG compressed LogLuv data");
        }
        TIFFGetField(tif.data(), TIFFTAG_SAMPLESPERPIXEL, &nSamples);
        TIFFSetField(tif.data(), TIFFTAG_SGILOGDATAFMT, SGILOGDATAFMT_FLOAT);
        TypeOfData = FLOATLOGLUV;
    }
        break;
    case PHOTOMETRIC_RGB:
    {
        qDebug("Photometric data: RGB");
        // read extra samples (# of alpha channels)
        if (TIFFGetField(tif.data(), TIFFTAG_EXTRASAMPLES, &extra_samples_per_pixel, &extra_sample_types) != 1)
        {
            extra_samples_per_pixel = 0;
        }
        TIFFGetField(tif.data(), TIFFTAG_SAMPLESPERPIXEL, &nSamples);
        bps = nSamples - extra_samples_per_pixel;
        has_alpha = (extra_samples_per_pixel == 1);
        // qDebug("nSamples=%d extra_samples_per_pixel=%d",nSamples,extra_samples_per_pixel);
        // qDebug("has alpha? %s", has_alpha ? "true" : "false");
        if (bps != 3)
        {
            qDebug ("TIFF: unsupported samples per pixel for RGB");
            throw std::runtime_error ("TIFF: unsupported samples per pixel for RGB");
        }
        if (!TIFFGetField(tif.data(), TIFFTAG_BITSPERSAMPLE, &bps) || (bps != 8 && bps != 16 && bps != 32))
        {
            qDebug ("TIFF: unsupported bits per sample for RGB");
            throw std::runtime_error ("TIFF: unsupported bits per sample for RGB");
        }

        switch (bps)
        {
        case 8:
            TypeOfData = BYTE;
            qDebug ("8bit per channel");
            break;
        case 16:
            TypeOfData = WORD;
            qDebug ("16bit per channel");
            break;
        default:
            TypeOfData = FLOAT;
            qDebug ("32bit float per channel");
            break;
        }
        ColorSpace = RGB;
    }
        break;
    case PHOTOMETRIC_SEPARATED:
    {
        qDebug("Photometric data: CMYK");
        TIFFGetField(tif.data(), TIFFTAG_SAMPLESPERPIXEL, &nSamples);
        qDebug() << "nSamples: " << nSamples;
        TIFFGetField(tif.data(), TIFFTAG_BITSPERSAMPLE, &bps);

        switch (bps)
        {
        case 8:
            TypeOfData = BYTE;
            qDebug ("8bit per channel");
            break;
        case 16:
            TypeOfData = WORD;
            qDebug ("16bit per channel");
            break;
        default:
            TypeOfData = FLOAT;
            qDebug ("32bit float per channel");
            break;
        }
        ColorSpace = CMYK;
    }
        break;
    default:
        //qFatal("Unsupported photometric type: %d",phot);
        throw std::runtime_error ("TIFF: unsupported photometric type");
    }

    if (!TIFFGetField(tif.data(), TIFFTAG_STONITS, &stonits))
        stonits = 1.;
}

pfs::Frame*
TiffReader::readIntoPfsFrame()
{
    qDebug() << "TiffReader::readIntoPfsFrame()";

    bool doTransform = false;
    // LuminanceOptions luminance_opts;
    // int camera_profile_opt = luminance_opts.getCameraProfile ();

    // will get automatigically cleaned on return of this function!
    ScopedCmsTransform xform;

//    if (camera_profile_opt == 1) // embedded
//    {
    ScopedCmsProfile hIn( GetTIFFProfile(tif.data()) );

    if (hIn)
    {
        qDebug () << "Found ICC profile";

        ScopedCmsProfile hsRGB( cmsCreate_sRGBProfile() );

        cmsUInt32Number cmsInputFormat = TYPE_CMYK_8;
        cmsUInt32Number cmsOutputFormat = TYPE_RGBA_8;
        cmsUInt32Number cmsIntent = INTENT_PERCEPTUAL;

        if (ColorSpace == RGB && TypeOfData == WORD)
        {
            cmsInputFormat = TYPE_RGB_16;
            cmsOutputFormat = TYPE_RGB_16;
        }
        else if (ColorSpace == RGB && TypeOfData == BYTE)
        {
            cmsInputFormat = TYPE_RGB_8;
            cmsOutputFormat = TYPE_RGB_8;
        }
        else if (ColorSpace == CMYK && TypeOfData == WORD)
        {
            cmsInputFormat = TYPE_CMYK_16;
            cmsOutputFormat = TYPE_RGBA_16;
        }
        else
        {
            cmsInputFormat = TYPE_CMYK_8;
            cmsOutputFormat = TYPE_RGBA_8;
        }

        xform.reset( cmsCreateTransform (hIn.data(), cmsInputFormat, hsRGB.data(), cmsOutputFormat, cmsIntent, 0) );
        if (xform)
        {
            doTransform = true;
            qDebug () << "Created transform";
        }
    }
#ifdef QT_DEBUG
    else
    {
        qDebug () << "No embedded profile found";
    }
#endif
//    }
//    else if (camera_profile_opt == 2)   // from file
//    {
//        QString profile_fname = luminance_opts.getCameraProfileFileName ();
//        qDebug () << "Camera profile: " << profile_fname;

//        if (!profile_fname.isEmpty ())
//        {
//            QByteArray ba( QFile::encodeName( profile_fname ) );

//            ScopedCmsProfile hsRGB( cmsCreate_sRGBProfile() );
//            ScopedCmsProfile hIn( cmsOpenProfileFromFile (ba.data (), "r") );

//            if ( hsRGB && hIn )
//            {
//                if (ColorSpace == RGB && TypeOfData == WORD)
//                    xform.reset( cmsCreateTransform (hIn.data(), TYPE_RGB_16, hsRGB.data(), TYPE_RGB_16, INTENT_PERCEPTUAL, 0) );
//                else if (ColorSpace == RGB && TypeOfData == BYTE)
//                    xform.reset( cmsCreateTransform (hIn.data(), TYPE_RGB_8, hsRGB.data(), TYPE_RGB_8, INTENT_PERCEPTUAL, 0) );
//                else if (ColorSpace == CMYK && TypeOfData == WORD)
//                    xform.reset( cmsCreateTransform (hIn.data(), TYPE_CMYK_16, hsRGB.data(), TYPE_RGBA_16, INTENT_PERCEPTUAL, 0) );
//                else
//                    xform.reset( cmsCreateTransform (hIn.data(), TYPE_CMYK_8, hsRGB.data(), TYPE_RGBA_8, INTENT_PERCEPTUAL, 0) );
//            }
//            doTransform = true;

//            qDebug () << "Created transform";
//        }
//    }

    pfs::Frame* frame = new pfs::Frame (width, height);

    pfs::Channel* Xc;
    pfs::Channel* Yc;
    pfs::Channel* Zc;
    frame->createXYZChannels (Xc, Yc, Zc);

    float* X = Xc->getRawData();
    float* Y = Yc->getRawData();
    float* Z = Zc->getRawData();

    //--- image length
    uint32 imagelength;
    TIFFGetField(tif.data(), TIFFTAG_IMAGELENGTH, &imagelength);

    emit maximumValue(imagelength);	//for QProgressDialog

    //--- image scanline size
    uint32 scanlinesize = TIFFScanlineSize(tif.data());
    std::vector<uchar> buf( scanlinesize );
    std::vector<uchar> buf2;
    if ( xform )
    {
        buf2.resize( scanlinesize );
    }

    qDebug () << "scanlinesize: " << scanlinesize;
    //--- read scan lines
    const int  image_width = width;

    for (uint32 row = 0; row < height; row++)
    {
        switch (TypeOfData)
        {
        case FLOAT:
        case FLOATLOGLUV:
        {
            float* buf_fp = reinterpret_cast<float*>(buf.data());

            TIFFReadScanline (tif.data(), buf_fp, row);
            for (int i = 0; i < image_width; i++)
            {
                X[row * image_width + i] = buf_fp[i * nSamples];
                Y[row * image_width + i] = buf_fp[i * nSamples + 1];
                Z[row * image_width + i] = buf_fp[i * nSamples + 2];
            }
        }
            break;
        case WORD:
        {
            uint16* buf_wp = reinterpret_cast<uint16*>(buf.data());

            TIFFReadScanline(tif.data(), buf_wp, row);
            if (doTransform)
            {
                uint16* buf_wp_2 = reinterpret_cast<uint16*>(buf2.data());

                cmsDoTransform(xform.data(), buf_wp, buf_wp_2, image_width);

                ::std::swap(buf_wp, buf_wp_2);
            }
            else if (ColorSpace == CMYK)
            {
                transform_to_rgb_16(buf_wp, buf_wp, scanlinesize, nSamples);
            }
            for (int i = 0; i < image_width; i++)
            {
                X[row * image_width + i] = buf_wp[i * nSamples];
                Y[row * image_width + i] = buf_wp[i * nSamples + 1];
                Z[row * image_width + i] = buf_wp[i * nSamples + 2];
            }
        }
            break;
        case BYTE:
        {
            uint8* buf_bp = reinterpret_cast<uint8*>(buf.data());

            TIFFReadScanline(tif.data(), buf_bp, row);
            if (doTransform)
            {
                uint8* buf_bp_2 = reinterpret_cast<uint8*>(buf2.data());

                cmsDoTransform(xform.data(), buf_bp, buf_bp_2, image_width);

                ::std::swap(buf_bp, buf_bp_2);
            }
            else if (ColorSpace == CMYK)
            {
                transform_to_rgb(buf_bp, buf_bp, scanlinesize, nSamples);
            }
            for (int i = 0; i < image_width; i++)
            {
                X[row * image_width + i] = powf(buf_bp[i * nSamples] * DIV_255, 2.2f); // why?
                Y[row * image_width + i] = powf(buf_bp[i * nSamples + 1] * DIV_255, 2.2f); // why?
                Z[row * image_width + i] = powf(buf_bp[i * nSamples + 2] * DIV_255, 2.2f); // why?
            }
        }
            break;
        }
        emit nextstep (row);	//for QProgressDialog
    }

    if (writeOnDisk)
    {
        assert (TypeOfData != FLOAT);
        assert (TypeOfData != FLOATLOGLUV);

        float scaleFactor = DIV_256;
        if ( TypeOfData == BYTE) scaleFactor = 1.0f;

        pfs::Channel *Xc, *Yc, *Zc;
        frame->createXYZChannels (Xc, Yc, Zc);

        float* X = Xc->getRawData();
        float* Y = Yc->getRawData();
        float* Z = Zc->getRawData();

        QImage remapped( image_width, imagelength, QImage::Format_RGB32);

        for (uint32 row = 0; row < height; ++row)
        {
            QRgb* line =  reinterpret_cast<QRgb*>(remapped.scanLine(row));
            for (uint32 col = 0; col < width; ++col)
            {
                line[col] = qRgb(static_cast<char>(*X * scaleFactor),
                                 static_cast<char>(*Y * scaleFactor),
                                 static_cast<char>(*Z * scaleFactor));

                X++; Y++; Z++;
            }
        }
        QFileInfo fi (fileName);
        QString fname = fi.completeBaseName () + ".thumb.jpg";

        remapped.scaledToHeight(imagelength / 10).save(tempFilesPath + "/" + fname);
    }

    //if (TypeOfData==FLOATLOGLUV)
    //  pfs::transformColorSpace( pfs::CS_XYZ, X,Y,Z, pfs::CS_RGB, X,Y,Z );
    return frame;
}

// given for granted that users of this function call it only after checking that TypeOfData==BYTE
QImage*
TiffReader::readIntoQImage()
{
    assert(TypeOfData == BYTE);

    // qDebug() << "TiffReader::readIntoQImage()";

    bool doTransform = false;
    // LuminanceOptions luminance_opts;
    // int camera_profile_opt = luminance_opts.getCameraProfile ();

    ScopedCmsProfile hIn( GetTIFFProfile(tif.data()) );
    ScopedCmsProfile hsRGB( cmsCreate_sRGBProfile() );

    ScopedCmsTransform xform;

    //    if (camera_profile_opt == 1) // embedded
    //    {
    if (hIn)
    {
        qDebug () << "Found ICC profile";

        cmsUInt32Number cmsInputFormat = TYPE_CMYK_8;
        cmsUInt32Number cmsOutputFormat = TYPE_RGBA_8;
        cmsUInt32Number cmsIntent = INTENT_PERCEPTUAL;

        if (has_alpha && ColorSpace == RGB && TypeOfData == BYTE)
        {
            cmsInputFormat = TYPE_RGBA_8;
            cmsOutputFormat = TYPE_RGBA_8;
        }
        else if (!has_alpha && ColorSpace == RGB && TypeOfData == BYTE)
        {
            cmsInputFormat = TYPE_RGB_8;
            cmsOutputFormat = TYPE_RGBA_8;
        }
        else if (ColorSpace == CMYK && TypeOfData == BYTE)
        {
            cmsInputFormat = TYPE_CMYK_8;
            cmsOutputFormat = TYPE_RGBA_8;
        }

        xform.reset( cmsCreateTransform (hIn.data(), cmsInputFormat, hsRGB.data(), cmsOutputFormat, cmsIntent, 0) );
        if ( xform ) doTransform = true;
    }
#ifdef QT_DEBUG
    else
    {
        qDebug () << "No embedded profile found";
    }
#endif
//    }
//    else if (camera_profile_opt == 2) // from file
//    {
//        QString profile_fname = luminance_opts.getCameraProfileFileName ();
//        qDebug () << "Camera profile: " << profile_fname;

//        if (!profile_fname.isEmpty ())
//        {
//            QByteArray ba( QFile::encodeName( profile_fname ) );

//            hsRGB.reset( cmsCreate_sRGBProfile () );
//            hIn.reset( cmsOpenProfileFromFile (ba.data (), "r") );

//            if ( hIn )
//            {
//                if (ColorSpace == RGB && TypeOfData == BYTE)
//                    xform.reset( cmsCreateTransform (hIn.data(), TYPE_RGBA_8, hsRGB.data(), TYPE_RGBA_8, INTENT_PERCEPTUAL, 0) );
//                else if (ColorSpace == CMYK && TypeOfData == BYTE)
//                    xform.reset( cmsCreateTransform (hIn.data(), TYPE_CMYK_8, hsRGB.data(), TYPE_RGBA_8, INTENT_PERCEPTUAL, 0) );

//                if ( xform ) doTransform = true;
//            }
//        }
//    }

    QScopedPointer<QImage> toReturn( new QImage(width, height, QImage::Format_ARGB32) );

    //--- image length
    uint32 imagelength;
    TIFFGetField (tif.data(), TIFFTAG_IMAGELENGTH, &imagelength);

    //--- image scanline size
    uint32 scanlinesize = TIFFScanlineSize(tif.data());

    qDebug() << "Scanlinesize:" << scanlinesize;

    std::vector<uint8> buffer(scanlinesize);
    std::vector<uint8> bufferConverted;
    if ( doTransform )
    {
        bufferConverted.resize((width << 2));
    }

    qDebug() << "Do Transform: " << doTransform;

    //--- read scan lines
    for (uint y = 0; y < height; y++)
    {
        QRgb* qImageData = reinterpret_cast<QRgb*>(toReturn->scanLine(y));
        uchar* pBuffer = buffer.data();

        TIFFReadScanline(tif.data(), pBuffer, y);
        if ( doTransform )
        {
            uchar* pBufferConverted = bufferConverted.data();

            cmsDoTransform(xform.data(), pBuffer, pBufferConverted, width);
            ::std::swap( pBuffer, pBufferConverted );
        }
        else if (!doTransform && ColorSpace == CMYK)
        {
            qDebug () << "Convert to RGB";

            transform_to_rgb(pBuffer, pBuffer, scanlinesize, nSamples);
        }

        if ( doTransform )
        {
            // If I have the CMS transform, I always have 4 components as output
            for (uint x = 0; x < width; x++)
            {
                size_t index = (x << 2);
                qImageData[x] = qRgba(pBuffer[index],
                                      pBuffer[index + 1],
                                      pBuffer[index + 2],
                                      has_alpha ? pBuffer[index + 3] : 0xFF);
            }
        }
        else
        {
            for (uint x = 0; x < width; x++)
            {
                qImageData[x] = qRgba(pBuffer[(x * nSamples)],
                                      pBuffer[(x * nSamples) + 1],
                                      pBuffer[(x * nSamples) + 2],
                                      has_alpha ? pBuffer[(x * nSamples) + 3] : 0xFF);
            }
        }
    }

    return toReturn.take();
}

TiffWriter::TiffWriter (const char *filename, pfs::Frame* f):
    tif(TIFFOpen (filename, "w")),
    ldrimage(0),
    pixmap(0),
    pfsFrame(f),
    width(f->getWidth()),
    height(f->getHeight())
{ 
    if (!tif)
    {
        throw std::runtime_error ("TIFF: could not open file for writing.");
    }

    TIFFSetField (tif.data(), TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField (tif.data(), TIFFTAG_IMAGELENGTH, height);
    TIFFSetField (tif.data(), TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (tif.data(), TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField (tif.data(), TIFFTAG_ROWSPERSTRIP, 1);
}

TiffWriter::TiffWriter (const char *filename, const quint16 * pix, int w, int h):
    tif(TIFFOpen (filename, "w")),
    ldrimage(0),
    pixmap(pix),
    pfsFrame(0),
    width(w),
    height(h)
{
    if (!tif)
    {
        throw std::runtime_error ("TIFF: could not open file for writing.");
    }

    TIFFSetField (tif.data(), TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField (tif.data(), TIFFTAG_IMAGELENGTH, height);
    TIFFSetField (tif.data(), TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (tif.data(), TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField (tif.data(), TIFFTAG_ROWSPERSTRIP, 1);
}

TiffWriter::TiffWriter (const char *filename, QImage * f):
    tif(TIFFOpen (filename, "w")),
    ldrimage(f),
    pixmap(0),
    pfsFrame(0),
    width(f->width()),
    height(f->height())
{
    if (!tif)
    {
        throw std::runtime_error ("TIFF: could not open file for writing.");
    }

    uint16 extras[1];
    extras[0] = EXTRASAMPLE_ASSOCALPHA;

    TIFFSetField (tif.data(), TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField (tif.data(), TIFFTAG_IMAGELENGTH, height);
    TIFFSetField (tif.data(), TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (tif.data(), TIFFTAG_SAMPLESPERPIXEL, 4);
    TIFFSetField (tif.data(), TIFFTAG_EXTRASAMPLES, 1, &extras);
    TIFFSetField (tif.data(), TIFFTAG_ROWSPERSTRIP, 1);
}

//write 32 bit float Tiff from pfs::Frame
int
TiffWriter::writeFloatTiff()
{
    assert(pfsFrame != 0);

    TIFFSetField (tif.data(), TIFFTAG_COMPRESSION, COMPRESSION_DEFLATE);	// TODO what about others?
    TIFFSetField (tif.data(), TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField (tif.data(), TIFFTAG_BITSPERSAMPLE, 32);

    pfs::Channel* Xc;
    pfs::Channel* Yc;
    pfs::Channel* Zc;

    pfsFrame->getXYZChannels(Xc, Yc, Zc);

    const float *X = Xc->getRawData ();
    const float *Y = Yc->getRawData ();
    const float *Z = Zc->getRawData ();

    tsize_t strip_size = TIFFStripSize (tif.data());
    tstrip_t strips_num = TIFFNumberOfStrips (tif.data());
    float *strip_buf = (float *) _TIFFmalloc (strip_size);	//enough space for a strip (row)
    if (!strip_buf)
    {
        throw std::runtime_error ("TIFF: error allocating buffer.");
    }

    emit maximumValue (strips_num);	// for QProgressDialog

    for (unsigned int s = 0; s < strips_num; s++)
    {
        for (unsigned int col = 0; col < width; col++)
        {
            strip_buf[3 * col + 0] = X[s * width + col];	//(*X)(col,s);
            strip_buf[3 * col + 1] = Y[s * width + col];	//(*Y)(col,s);
            strip_buf[3 * col + 2] = Z[s * width + col];	//(*Z)(col,s);
        }
        if (TIFFWriteEncodedStrip(tif.data(), s, strip_buf, strip_size) == 0)
        {
            qDebug ("error writing strip");

            return -1;
        }
        else
        {
            emit nextstep (s);	// for QProgressDialog
        }
    }
    _TIFFfree (strip_buf);
    return 0;
}

//write LogLUv Tiff from pfs::Frame
int
TiffWriter::writeLogLuvTiff ()
{
    assert(pfsFrame != 0);

    TIFFSetField (tif.data(), TIFFTAG_COMPRESSION, COMPRESSION_SGILOG);
    TIFFSetField (tif.data(), TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_LOGLUV);
    TIFFSetField (tif.data(), TIFFTAG_SGILOGDATAFMT, SGILOGDATAFMT_FLOAT);
    TIFFSetField (tif.data(), TIFFTAG_STONITS, 1.);	/* not known */

    pfs::Channel* Xc;
    pfs::Channel* Yc;
    pfs::Channel* Zc;

    pfsFrame->getXYZChannels(Xc, Yc, Zc);

    const float *X = Xc->getRawData ();
    const float *Y = Yc->getRawData ();
    const float *Z = Zc->getRawData ();

    tsize_t strip_size = TIFFStripSize (tif.data());
    tstrip_t strips_num = TIFFNumberOfStrips (tif.data());
    float *strip_buf = (float *) _TIFFmalloc (strip_size);	// enough space for a strip
    if (!strip_buf)
    {
        throw std::runtime_error ("TIFF: error allocating buffer.");
    }

    emit maximumValue (strips_num);	// for QProgressDialog

    for (unsigned int s = 0; s < strips_num; s++)
    {
        for (unsigned int col = 0; col < width; col++)
        {
            strip_buf[3 * col + 0] = X[s * width + col];	//(*X)(col,s);
            strip_buf[3 * col + 1] = Y[s * width + col];	//(*Y)(col,s);
            strip_buf[3 * col + 2] = Z[s * width + col];	//(*Z)(col,s);
        }
        if (TIFFWriteEncodedStrip(tif.data(), s, strip_buf, strip_size) == 0)
        {
            qDebug ("error writing strip");
            return -1;
        }
        else
        {
            emit nextstep (s);	// for QProgressDialog
        }
    }

    _TIFFfree (strip_buf);
    return 0;
}

int
TiffWriter::write8bitTiff ()
{
    assert(ldrimage != NULL);
    if (ldrimage == NULL)
        throw std::runtime_error ("TIFF: QImage was not set correctly");

    ScopedCmsProfile hsRGB( cmsCreate_sRGBProfile() );
    cmsUInt32Number profileSize = 0;
    cmsSaveProfileToMem (hsRGB.data(), NULL, &profileSize);	// get the size

    std::vector<char> embedBuffer(profileSize);

    cmsSaveProfileToMem(hsRGB.data(),
                        reinterpret_cast<void*>(embedBuffer.data()),
                        &profileSize);

    TIFFSetField(tif.data(), TIFFTAG_ICCPROFILE, profileSize,
                 reinterpret_cast<void*>(embedBuffer.data()) );

    TIFFSetField(tif.data(), TIFFTAG_COMPRESSION, COMPRESSION_DEFLATE);	// TODO what about others?
    TIFFSetField(tif.data(), TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tif.data(), TIFFTAG_BITSPERSAMPLE, 8);

    tsize_t strip_size = TIFFStripSize(tif.data());
    tstrip_t strips_num = TIFFNumberOfStrips(tif.data());

    char *strip_buf = (char *)_TIFFmalloc (strip_size);	//enough space for a strip
    if (!strip_buf)
    {
        throw std::runtime_error ("TIFF: error allocating buffer");
    }

    QRgb *ldrpixels = reinterpret_cast<QRgb*>(ldrimage->bits ());

    emit maximumValue (strips_num);	// for QProgressDialog
    for (unsigned int s = 0; s < strips_num; s++)
    {
        for (unsigned int col = 0; col < width; col++)
        {
            strip_buf[4 * col + 0] = qRed (ldrpixels[width * s + col]);
            strip_buf[4 * col + 1] = qGreen (ldrpixels[width * s + col]);
            strip_buf[4 * col + 2] = qBlue (ldrpixels[width * s + col]);
            strip_buf[4 * col + 3] = qAlpha (ldrpixels[width * s + col]);
        }
        if (TIFFWriteEncodedStrip(tif.data(), s, strip_buf, strip_size) == 0)
        {
            qDebug ("error writing strip");
            return -1;
        }
        else
        {
            emit nextstep (s);	// for QProgressDialog
        }
    }
    _TIFFfree (strip_buf);

    return 0;
}

int
TiffWriter::write16bitTiff ()
{
    assert(pixmap != NULL);

    if (pixmap == NULL)
    {
        throw std::runtime_error ("TIFF: 16 bits pixmap was not set correctly");
    }

    ScopedCmsProfile hsRGB( cmsCreate_sRGBProfile() );
    cmsUInt32Number profileSize = 0;
    cmsSaveProfileToMem (hsRGB.data(), NULL, &profileSize);	// get the size

    std::vector<char> embedBuffer(profileSize);

    cmsSaveProfileToMem(hsRGB.data(),
                        reinterpret_cast<void*>(embedBuffer.data()),
                        &profileSize);

    TIFFSetField(tif.data(), TIFFTAG_ICCPROFILE, profileSize,
                 reinterpret_cast<void*>(embedBuffer.data()) );

    TIFFSetField(tif.data(), TIFFTAG_COMPRESSION, COMPRESSION_DEFLATE);	// TODO what about others?
    TIFFSetField(tif.data(), TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tif.data(), TIFFTAG_BITSPERSAMPLE, 16);

    tsize_t strip_size = TIFFStripSize (tif.data());
    tstrip_t strips_num = TIFFNumberOfStrips (tif.data());

    quint16 *strip_buf = (quint16 *) _TIFFmalloc (strip_size);	//enough space for a strip
    if (!strip_buf)
    {
        throw std::runtime_error ("TIFF: error allocating buffer");
    }

    emit maximumValue (strips_num);	// for QProgressDialog

    for (unsigned int s = 0; s < strips_num; s++)
    {
        for (unsigned int col = 0; col < width; col++)
        {
            strip_buf[3 * col] = pixmap[3 * (width * s + col)];
            strip_buf[3 * col + 1] = pixmap[3 * (width * s + col) + 1];
            strip_buf[3 * col + 2] = pixmap[3 * (width * s + col) + 2];
        }
        if (TIFFWriteEncodedStrip(tif.data(), s, strip_buf, strip_size) == 0)
        {
            qDebug ("error writing strip");

            return -1;
        }
        else
        {
            emit nextstep (s);	// for QProgressDialog
        }
    }
    _TIFFfree (strip_buf);

    return 0;
}

int
TiffWriter::writePFSFrame16bitTiff()
{
    assert (pfsFrame != 0);

    TIFFSetField (tif.data(), TIFFTAG_COMPRESSION, COMPRESSION_DEFLATE);	// TODO what about others?
    TIFFSetField (tif.data(), TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField (tif.data(), TIFFTAG_BITSPERSAMPLE, 16);

    pfs::Channel* Xc;
    pfs::Channel* Yc;
    pfs::Channel* Zc;

    pfsFrame->getXYZChannels(Xc, Yc, Zc);

    const float *X = Xc->getRawData ();
    const float *Y = Yc->getRawData ();
    const float *Z = Zc->getRawData ();

    tsize_t strip_size = TIFFStripSize (tif.data());
    tstrip_t strips_num = TIFFNumberOfStrips (tif.data());
    quint16 *strip_buf = (quint16 *) _TIFFmalloc (strip_size);	//enough space for a strip (row)
    if (!strip_buf)
    {
        throw std::runtime_error ("TIFF: error allocating buffer.");
    }

    emit maximumValue (strips_num);	// for QProgressDialog

    for (unsigned int s = 0; s < strips_num; s++)
    {
        for (unsigned int col = 0; col < width; col++)
        {
            strip_buf[3 * col + 0] = (qint16) X[s * width + col];	//(*X)(col,s);
            strip_buf[3 * col + 1] = (qint16) Y[s * width + col];	//(*Y)(col,s);
            strip_buf[3 * col + 2] = (qint16) Z[s * width + col];	//(*Z)(col,s);
        }
        if (TIFFWriteEncodedStrip(tif.data(), s, strip_buf, strip_size) == 0)
        {
            qDebug ("error writing strip");

            return -1;
        }
        else
        {
            emit nextstep (s);	// for QProgressDialog
        }
    }
    _TIFFfree (strip_buf);

    return 0;
}

