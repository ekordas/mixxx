/* -*- mode:C++; indent-tabs-mode:t; tab-width:8; c-basic-offset:4; -*- */
/***************************************************************************
                          soundsourceffmpeg.cpp -  ffmpeg decoder
                             -------------------
    copyright            : (C) 2007 by Cedric GESTES
                           (C) 2012-2014 by Tuukka Pasanen
    email                : tuukka.pasanen@ilmi.fi

    This one tested with FFMPEG 0.10/0.11/1.0/1.1/1.2/2,0/2,1/GIT
                         Libav  0.8/9/GIT
    FFMPEG below 0.10 WON'T work. If you like to it work you can
    allways send a patch but it's mostly not worth it!
***************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "soundsourceffmpeg.h"

#include "trackmetadata.h"

#include <QBuffer>
#include <QtDebug>

#include <vector>

#define SOUNDSOURCEFFMPEG_CACHESIZE 1000
#define SOUNDSOURCEFFMPEG_POSDISTANCE ((1024 * 1000) / 8)

SoundSourceFFmpeg::SoundSourceFFmpeg(QString filename)
    : Mixxx::SoundSource(filename)
    , m_pFormatCtx(NULL)
    , m_iAudioStream(-1)
    , m_pCodecCtx(NULL)
    , m_pCodec(NULL)
    , m_pResample(NULL)
    , m_iCurrentMixxTs(0)
    , m_bIsSeeked(false)
    , m_lCacheBytePos(0)
    , m_lCacheStartByte(0)
    , m_lCacheEndByte(0)
    , m_lCacheLastPos(0)
    , m_lLastStoredPos(0)
    , m_lStoredSeekPoint(-1) {
}

SoundSourceFFmpeg::~SoundSourceFFmpeg() {
    clearCache();

    if (m_pCodecCtx != NULL) {
        qDebug() << "~SoundSourceFFmpeg(): Clear FFMPEG stuff";
        avcodec_close(m_pCodecCtx);
        avformat_close_input(&m_pFormatCtx);
        av_free(m_pFormatCtx);
    }

    if (m_pResample != NULL) {
        qDebug() << "~SoundSourceFFmpeg(): Delete FFMPEG Resampler";
        delete m_pResample;
    }

    while (m_SJumpPoints.size() > 0) {
        ffmpegLocationObject* l_SRmJmp = m_SJumpPoints[0];
        m_SJumpPoints.remove(0);
        free(l_SRmJmp);
    }
}

bool SoundSourceFFmpeg::clearCache() {
    struct ffmpegCacheObject *l_SRmObj = NULL;

    while (m_SCache.size() > 0) {
        l_SRmObj = m_SCache[0];
        m_SCache.remove(0);
        free(l_SRmObj->bytes);
        free(l_SRmObj);
    }

    return true;
}

bool SoundSourceFFmpeg::readFramesToCache(unsigned int count, qint64 offset) {
    unsigned int l_iCount = 0;
    qint32 l_iRet = 0;
    AVPacket l_SPacket;
    AVFrame *l_pFrame = NULL;
    bool l_iStop = false;
    int l_iFrameFinished = 0;
    struct ffmpegCacheObject *l_SObj = NULL;
    struct ffmpegCacheObject *l_SRmObj = NULL;
    bool m_bUnique = false;
    qint64 l_lLastPacketPos = -1;
    int l_iError = 0;
    int l_iFrameCount = 0;

    l_iCount = count;

    l_SPacket.data = NULL;
    l_SPacket.size = 0;


    while (l_iCount > 0) {
        if (l_pFrame != NULL) {
          l_iFrameCount --;
// FFMPEG 2.2 3561060 anb beyond
#if LIBAVCODEC_VERSION_INT >= 3561060
            av_frame_free(&l_pFrame);
// FFMPEG 0.11 and below
#elif LIBAVCODEC_VERSION_INT <= 3544932
            av_free(l_pFrame);
// FFMPEG 1.0 - 2.1
#else
            avcodec_free_frame(&l_pFrame);
#endif
            l_pFrame = NULL;

        }

        if (l_iStop == true) {
            break;
        }
        l_iFrameCount ++;
        av_init_packet(&l_SPacket);
#if LIBAVCODEC_VERSION_INT < 3617792
        l_pFrame = avcodec_alloc_frame();
#else
        l_pFrame = av_frame_alloc();
#endif

        if (av_read_frame(m_pFormatCtx, &l_SPacket) >= 0) {
            if (l_SPacket.stream_index == m_iAudioStream) {
                if (m_lStoredSeekPoint > 0) {
                    // Seek for correct jump point
                    if (m_lStoredSeekPoint > l_SPacket.pos &&
                            m_lStoredSeekPoint >= SOUNDSOURCEFFMPEG_POSDISTANCE) {
                        av_free_packet(&l_SPacket);
                        l_SPacket.data = NULL;
                        l_SPacket.size = 0;
                        continue;
                    }
                    m_lStoredSeekPoint = -1;
                }

                l_iRet = avcodec_decode_audio4(m_pCodecCtx,l_pFrame,&l_iFrameFinished,
                                               &l_SPacket);

                if (l_iRet <= 0) {
                    // An error or EOF occured,index break out and return what
                    // we have so far.
                    qDebug() << "EOF!";
                    l_iStop = true;
                    continue;
                } else {
                    l_iRet = 0;
                    l_SObj = (struct ffmpegCacheObject *)malloc(sizeof(struct ffmpegCacheObject));
                    if (l_SObj == NULL) {
                        qDebug() << "SoundSourceFFmpeg::readFramesToCache: Not enough memory!";
                        l_iStop = true;
                        continue;
                    }
                    memset(l_SObj, 0x00, sizeof(struct ffmpegCacheObject));
                    l_iRet = m_pResample->reSample(l_pFrame, &l_SObj->bytes);

                    if (l_iRet > 0) {
                        // Remove from cache
                        if (m_SCache.size() >= (SOUNDSOURCEFFMPEG_CACHESIZE - 10)) {
                            l_SRmObj = m_SCache[0];
                            m_SCache.remove(0);
                            free(l_SRmObj->bytes);
                            free(l_SRmObj);
                        }

                        // Add to cache and store byte place to memory
                        m_SCache.append(l_SObj);
                        l_SObj->startByte = m_lCacheBytePos / 2;
                        l_SObj->length = l_iRet / 2;
                        m_lCacheBytePos += l_iRet;

                        // Ogg/Opus have packages pos that have many
                        // audio frames so seek next unique pos..
                        if (l_SPacket.pos != l_lLastPacketPos) {
                            m_bUnique = true;
                            l_lLastPacketPos = l_SPacket.pos;
                        }

                        // If we are over last storepos and we have read more than jump point need is and pos is unique we store it to memory
                        if (m_lCacheBytePos > m_lLastStoredPos &&
                                m_lCacheBytePos > (SOUNDSOURCEFFMPEG_POSDISTANCE + m_lLastStoredPos) &&
                                m_bUnique == true) {
                            struct ffmpegLocationObject  *l_SJmp = (struct ffmpegLocationObject  *)malloc(
                                    sizeof(struct ffmpegLocationObject));
                            m_lLastStoredPos = m_lCacheBytePos;
                            l_SJmp->startByte = m_lCacheBytePos / 2;
                            l_SJmp->pos = l_SPacket.pos;
                            l_SJmp->pts = l_SPacket.pts;
                            m_SJumpPoints.append(l_SJmp);
                            m_bUnique = false;
                        }

                        if (offset < 0 || (quint64) offset <= (m_lCacheBytePos / 2)) {
                            l_iCount --;
                        }
                    } else {
                        free(l_SObj);
                        l_SObj = NULL;
                        qDebug() <<
                                 "SoundSourceFFmpeg::readFramesToCache: General error in audio decode:" <<
                                 l_iRet;
                    }
                }

                av_free_packet(&l_SPacket);
                l_SPacket.data = NULL;
                l_SPacket.size = 0;

            } else {
                l_iError ++;
                if (l_iError == 5) {
                    // Stream end and we couldn't read enough frames
                    l_iStop = true;
                }
            }


        } else {
            qDebug() << "SoundSourceFFmpeg::readFramesToCache: Packet too big or File end";
            l_iStop = true;
        }

    }

    if (l_pFrame != NULL) {
      l_iFrameCount --;
// FFMPEG 2.2 3561060 anb beyond
#if LIBAVCODEC_VERSION_INT >= 3561060
          av_frame_unref(l_pFrame);
          av_frame_free(&l_pFrame);
// FFMPEG 0.11 and below
#elif LIBAVCODEC_VERSION_INT <= 3544932
          av_free(l_pFrame);
// FFMPEG 1.0 - 2.1
#else
          avcodec_free_frame(&l_pFrame);
#endif
          l_pFrame = NULL;

    }

    if (l_iFrameCount > 0) {
       qDebug() << "SoundSourceFFmpeg::readFramesToCache(): Frame balance is not 0 it is: " << l_iFrameCount;
    }

    l_SObj = m_SCache.first();
    m_lCacheStartByte = l_SObj->startByte;
    l_SObj = m_SCache.last();
    m_lCacheEndByte = (l_SObj->startByte + l_SObj->length);

    if (!l_iCount) {
        return true;
    } else {
        return false;
    }
}

bool SoundSourceFFmpeg::getBytesFromCache(char *buffer, quint64 offset,
        quint64 size) {
    struct ffmpegCacheObject *l_SObj = NULL;
    quint32 l_lPos = 0;
    quint32 l_lLeft = 0;
    quint32 l_lOffset = 0;
    quint32 l_lBytesToCopy = 0;

    if (offset >= m_lCacheStartByte) {
        if (m_lCacheLastPos == 0) {
            m_lCacheLastPos = m_SCache.size() - 1;
        }
        for (l_lPos = m_lCacheLastPos; l_lPos > 0; l_lPos --) {
            l_SObj = m_SCache[l_lPos];
            if ((l_SObj->startByte + l_SObj->length) < offset) {
                break;
            }
        }

        l_SObj = m_SCache[l_lPos];

        l_lLeft = (size * 2);
        memset(buffer, 0x00, l_lLeft);
        while (l_lLeft > 0) {

            if (l_SObj == NULL || (l_lPos + 5) > (unsigned int)m_SCache.size()) {
                offset = l_SObj->startByte;
                if (readFramesToCache(50, -1) == false) {
                    return false;
                }
                for (l_lPos = (m_SCache.size() - 50); l_lPos > 0; l_lPos --) {
                    l_SObj = m_SCache[l_lPos];
                    if ((l_SObj->startByte + l_SObj->length) < offset) {
                        break;
                    }
                }
                l_SObj = m_SCache[l_lPos];
                continue;
            }

            if (l_SObj->startByte <= offset) {
                l_lOffset = (offset - l_SObj->startByte) * 2;
            }

            if (l_lOffset >= (l_SObj->length * 2)) {
                l_SObj = m_SCache[++ l_lPos];
                continue;
            }

            if (l_lLeft > (l_SObj->length * 2)) {
                l_lBytesToCopy = ((l_SObj->length * 2)  - l_lOffset);
                memcpy(buffer, (l_SObj->bytes + l_lOffset), l_lBytesToCopy);
                l_lOffset = 0;
                buffer += l_lBytesToCopy;
                l_lLeft -= l_lBytesToCopy;
            } else {
                memcpy(buffer, l_SObj->bytes, l_lLeft);
                l_lLeft = 0;
            }

            l_SObj = m_SCache[++ l_lPos];
        }

        m_lCacheLastPos = --l_lPos;
        return true;
    }

    return false;
}

Result SoundSourceFFmpeg::open() {
    unsigned int i;
    AVDictionary *l_iFormatOpts = NULL;

    qDebug() << "New SoundSourceFFmpeg :" << getFilename();

    m_pFormatCtx = avformat_alloc_context();

#if LIBAVCODEC_VERSION_INT < 3622144
    m_pFormatCtx->max_analyze_duration = 999999999;
#endif

    // Open file and make m_pFormatCtx
    if (avformat_open_input(&m_pFormatCtx, getFilename().toLocal8Bit().constData(), NULL,
                            &l_iFormatOpts)!=0) {
        qDebug() << "av_open_input_file: cannot open" << getFilename();
        return ERR;
    }


#if LIBAVCODEC_VERSION_INT > 3544932
    av_dict_free(&l_iFormatOpts);
#endif


    // Retrieve stream information
    if (avformat_find_stream_info(m_pFormatCtx, NULL)<0) {
        qDebug() << "av_find_stream_info: cannot open" << getFilename();
        return ERR;
    }


    //debug only (Enable if needed)
    //av_dump_format(m_pFormatCtx, 0, getFilename().toLocal8Bit().constData(), false);

    // Find the first video stream
    m_iAudioStream=-1;

    for (i=0; i<m_pFormatCtx->nb_streams; i++)
        if (m_pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
            m_iAudioStream=i;
            break;
        }
    if (m_iAudioStream==-1) {
        qDebug() << "ffmpeg: cannot find an audio stream: cannot open"
                 << getFilename();
        return ERR;
    }

    // Get a pointer to the codec context for the video stream
    m_pCodecCtx=m_pFormatCtx->streams[m_iAudioStream]->codec;

    // Find the decoder for the audio stream
    if (!(m_pCodec=avcodec_find_decoder(m_pCodecCtx->codec_id))) {
        qDebug() << "ffmpeg: cannot find a decoder for" << getFilename();
        return ERR;
    }

    if (avcodec_open2(m_pCodecCtx, m_pCodec, NULL)<0) {
        qDebug() << "ffmpeg:  cannot open" << getFilename();
        return ERR;
    }

    m_pResample = new EncoderFfmpegResample(m_pCodecCtx);
    m_pResample->open(m_pCodecCtx->sample_fmt, AV_SAMPLE_FMT_S16);

    this->setChannelCount(m_pCodecCtx->channels);
    this->setFrameRate(m_pCodecCtx->sample_rate);
    this->setFrameCount((m_pFormatCtx->duration * m_pCodecCtx->sample_rate) / AV_TIME_BASE);

    qDebug() << "ffmpeg: Samplerate: " << this->getFrameRate() << ", Channels: " <<
             this->getChannelCount() << "\n";
    if (this->getChannelCount() > 2) {
        qDebug() << "ffmpeg: No support for more than 2 channels!";
        return ERR;
    }

    return OK;
}

Mixxx::AudioSource::diff_type SoundSourceFFmpeg::seekFrame(diff_type frameIndex) {
    const diff_type filepos = frames2samples(frameIndex);

    int ret = 0;
    qint64 i = 0;

    if (filepos < 0 || (unsigned long) filepos < m_lCacheStartByte) {
        ret = avformat_seek_file(m_pFormatCtx,
                                 m_iAudioStream,
                                 0,
                                 32767 * 2,
                                 32767 * 2,
                                 AVSEEK_FLAG_BACKWARD);


        if (ret < 0) {
            qDebug() << "SoundSourceFFmpeg::seek: Can't seek to 0 byte!";
            return -1;
        }

        clearCache();
        m_SCache.clear();
        m_lCacheStartByte = 0;
        m_lCacheEndByte = 0;
        m_lCacheLastPos = 0;
        m_lCacheBytePos = 0;
        m_lStoredSeekPoint = -1;


        // Try to find some jump point near to
        // where we are located so we don't needed
        // to try guess it
        if (filepos >= SOUNDSOURCEFFMPEG_POSDISTANCE) {
            for (i = 0; i < m_SJumpPoints.size(); i ++) {
                if (m_SJumpPoints[i]->startByte >= (unsigned long) filepos && i > 2) {
                    m_lCacheBytePos = m_SJumpPoints[i - 2]->startByte * 2;
                    m_lStoredSeekPoint = m_SJumpPoints[i - 2]->pos;
                    break;
                }
            }
        }

        if (filepos == 0) {
            readFramesToCache((SOUNDSOURCEFFMPEG_CACHESIZE - 50), -1);
        } else {
            readFramesToCache((SOUNDSOURCEFFMPEG_CACHESIZE / 2), filepos);
        }
    }


    if (m_lCacheEndByte <= (unsigned long) filepos) {
        readFramesToCache(100, filepos);
    }

    m_iCurrentMixxTs = filepos;

    m_bIsSeeked = TRUE;

    return frameIndex;
}

unsigned int SoundSourceFFmpeg::read(unsigned long size, SAMPLE* destination) {

    if (m_SCache.size() == 0) {
        // Make sure we allways start at begining and cache have some
        // material that we can consume.
        seekFrame(0);
        m_bIsSeeked = FALSE;
    }

    getBytesFromCache((char *)destination, m_iCurrentMixxTs, size);


    //  As this is also Hack
    // If we don't seek like we don't on analyzer.. keep
    // place in mind..
    if (m_bIsSeeked == FALSE) {
        m_iCurrentMixxTs += size;
    }

    m_bIsSeeked = FALSE;
    return size;

}

Mixxx::AudioSource::size_type SoundSourceFFmpeg::readFrameSamplesInterleaved(size_type frameCount,
        sample_type* sampleBuffer) {
    // This is just a hack that simply reuses existing
    // functionality. Sample data should be resampled
    // directly into AV_SAMPLE_FMT_FLT instead of
    // AV_SAMPLE_FMT_S16!
    typedef std::vector<SAMPLE> TempBuffer;
    TempBuffer tempBuffer(frames2samples(frameCount));
    const size_type readSamples = read(tempBuffer.size(), &tempBuffer[0]);
    for (size_type i = 0; i < readSamples; ++i) {
        sampleBuffer[i] = SAMPLE_clampSymmetric(tempBuffer[i]) / sample_type(SAMPLE_MAX);
    }
    return samples2frames(readSamples);
}

Result SoundSourceFFmpeg::parseMetadata(Mixxx::TrackMetadata* pMetadata) {
    qDebug() << "ffmpeg: SoundSourceFFmpeg::parseMetadata" << getFilename();

    AVFormatContext *FmtCtx = avformat_alloc_context();
    AVCodecContext *CodecCtx;
    AVDictionaryEntry *FmtTag = NULL;
    unsigned int i;
    AVDictionary *l_iFormatOpts = NULL;

    if (avformat_open_input(&FmtCtx, getFilename().toLocal8Bit().constData(), NULL,
                            &l_iFormatOpts) !=0) {
        qDebug() << "av_open_input_file: cannot open" << getFilename();
        return ERR;
    }

#ifndef CODEC_ID_MP3
    if (LIBAVFORMAT_VERSION_INT > 3540580 && l_iFormatOpts != NULL) {
        av_dict_free(&l_iFormatOpts);
    }
#endif

#if LIBAVCODEC_VERSION_INT < 3622144
    FmtCtx->max_analyze_duration = 999999999;
#endif

    // Retrieve stream information
    if (avformat_find_stream_info(FmtCtx, NULL)<0) {
        qDebug() << "av_find_stream_info: Can't find metadata" <<
                getFilename();
        return ERR;
    }

    for (i=0; i<FmtCtx->nb_streams; i++)
        if (FmtCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
            m_iAudioStream=i;
            break;
        }
    if (m_iAudioStream==-1) {
        qDebug() << "cannot find an audio stream: Can't find stream" <<
                getFilename();
        return ERR;
    }

    // Get a pointer to the codec context for the video stream
    CodecCtx=FmtCtx->streams[m_iAudioStream]->codec;

    while ((FmtTag = av_dict_get(FmtCtx->metadata, "", FmtTag,
                                 AV_DICT_IGNORE_SUFFIX))) {
        QString strValue (QString::fromUtf8 (FmtTag->value));

        // TODO: More sophisticated metadata reading
        if (!strncmp(FmtTag->key, "artist", 7)) {
            pMetadata->setArtist(strValue);
        } else if (!strncmp(FmtTag->key, "title", 5)) {
            pMetadata->setTitle(strValue);
        } else if (!strncmp(FmtTag->key, "album_artist", 12)) {
            pMetadata->setAlbumArtist(strValue);
        } else if (!strncmp(FmtTag->key, "albumartist", 11)) {
            pMetadata->setAlbumArtist(strValue);
        } else if (!strncmp(FmtTag->key, "album", 5)) {
            pMetadata->setAlbum(strValue);
        } else if (!strncmp(FmtTag->key, "TOAL", 4)) {
            pMetadata->setAlbum(strValue);
        } else if (!strncmp(FmtTag->key, "date", 4)) {
            pMetadata->setYear(strValue);
        } else if (!strncmp(FmtTag->key, "genre", 5)) {
            pMetadata->setGenre(strValue);
        } else if (!strncmp(FmtTag->key, "comment", 7)) {
            pMetadata->setComment(strValue);
        }


    }

    while ((FmtTag = av_dict_get(FmtCtx->streams[m_iAudioStream]->metadata, "",
                                 FmtTag, AV_DICT_IGNORE_SUFFIX))) {
        // Convert the value from UTF-8.
        QString strValue (QString::fromUtf8 (FmtTag->value));

        if (!strncmp(FmtTag->key, "ARTIST", 7)) {
            pMetadata->setArtist(strValue);
        } else if (!strncmp(FmtTag->key, "ALBUM", 5)) {
            pMetadata->setAlbum(strValue);
        } else if (!strncmp(FmtTag->key, "YEAR", 4)) {
            pMetadata->setYear(strValue);
        } else if (!strncmp(FmtTag->key, "GENRE", 5)) {
            pMetadata->setGenre(strValue);
        } else if (!strncmp(FmtTag->key, "TITLE", 5)) {
            pMetadata->setTitle(strValue);
        } else if (!strncmp(FmtTag->key, "REPLAYGAIN_TRACK_PEAK", 20)) {
        } else if (!strncmp(FmtTag->key, "REPLAYGAIN_TRACK_GAIN", 20)) {
            pMetadata->setReplayGainString (strValue);
        } else if (!strncmp(FmtTag->key, "REPLAYGAIN_ALBUM_PEAK", 20)) {
        } else if (!strncmp(FmtTag->key, "REPLAYGAIN_ALBUM_GAIN", 20)) {
        }


    }

    pMetadata->setChannels(CodecCtx->channels);
    pMetadata->setSampleRate(CodecCtx->sample_rate);
    pMetadata->setBitrate(CodecCtx->bit_rate / 1000);
    pMetadata->setDuration(FmtCtx->duration / AV_TIME_BASE);

    avcodec_close(CodecCtx);
    avformat_close_input(&FmtCtx);
    av_free(FmtCtx);

    return OK;
}

QImage SoundSourceFFmpeg::parseCoverArt() {
    // currently not implemented
    return QImage();
}

QList<QString> SoundSourceFFmpeg::supportedFileExtensions() {
    QList<QString> list;
    AVInputFormat *l_SInputFmt  = NULL;

    while ((l_SInputFmt = av_iformat_next(l_SInputFmt))) {
        if (l_SInputFmt->name == NULL) {
            break;
        }

        if (!strcmp(l_SInputFmt->name, "flac")) {
            list.append("flac");
        } else if (!strcmp(l_SInputFmt->name, "ogg")) {
            list.append("ogg");
        } else if (!strcmp(l_SInputFmt->name, "mov,mp4,m4a,3gp,3g2,mj2")) {
            list.append("m4a");
        } else if (!strcmp(l_SInputFmt->name, "mp4")) {
            list.append("mp4");
        } else if (!strcmp(l_SInputFmt->name, "mp3")) {
            list.append("mp3");
        } else if (!strcmp(l_SInputFmt->name, "aac")) {
            list.append("aac");
        } else if (!strcmp(l_SInputFmt->name, "opus") ||
                   !strcmp(l_SInputFmt->name, "libopus")) {
            list.append("opus");
        } else if (!strcmp(l_SInputFmt->name, "wma")) {
            list.append("wma");
        }
    }

    return list;
}
