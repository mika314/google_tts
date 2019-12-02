#include "decode_audio.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <vector>
#ifndef INT64_C
#define INT64_C(c) (c##LL)
#define UINT64_C(c) (c##ULL)
#endif
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

struct MyIo
{
  std::string buff;
  size_t pos{0};
};

namespace
{
  int my_read(void *opaque, unsigned char *buf, int buf_size)
  {
    MyIo &myIo = *(MyIo *)opaque;
    buf_size = std::min(static_cast<int>(myIo.buff.size() - myIo.pos), buf_size);
    memcpy(buf, myIo.buff.data() + myIo.pos, buf_size);
    myIo.pos += buf_size;
    return buf_size;
  }

  int64_t my_seek(void *opaque, int64_t offset, int whence)
  {
    MyIo &myIo = *(MyIo *)opaque;
    switch (whence)
    {
    case SEEK_SET: myIo.pos = offset; break;
    case SEEK_CUR: myIo.pos += offset; break;
    case SEEK_END: myIo.pos = myIo.buff.size() + offset; break;
    case AVSEEK_SIZE: return myIo.buff.size();
    }
    return myIo.pos;
  }
} // namespace

std::vector<std::int16_t> decodeAudio(const std::string &compressedAudio)
{
  std::vector<std::int16_t> audio;
#if 1
  av_register_all();
  AVFormatContext *formatContext = avformat_alloc_context();
  unsigned char *buff = (unsigned char *)av_malloc(4096);

  MyIo myIo;
  myIo.buff = compressedAudio;

  AVIOContext *ctx = avio_alloc_context(buff, 4096, 0, &myIo, &my_read, nullptr, &my_seek);
  formatContext->pb = ctx;

  int len = avformat_open_input(&formatContext, "from memory", nullptr, nullptr);

  if (len != 0)
  {
    std::cerr << "Could not open input from memory" << std::endl;
    throw - 0x10;
  }

  if (avformat_find_stream_info(formatContext, NULL) < 0)
  {
    std::cerr << "Could not read stream information from memory\n";
    throw - 0x11;
  }
  // av_dump_format(formatContext, 0, "from memory", 0);

  int audioStreamIndex = -1;

  for (unsigned i = 0; i < formatContext->nb_streams; ++i)
    if (formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
    {
      if (audioStreamIndex == -1)
        audioStreamIndex = i;
    }
  if (audioStreamIndex == -1)
  {
    std::cerr << "File does not have audio stream" << std::endl;
    throw - 0x34;
  }

  auto codec = formatContext->streams[audioStreamIndex]->codec;
  AVCodecContext *audioDecodec;
  {
    if (codec->codec_id == 0)
    {
      std::cerr << "-0x30" << std::endl;
      throw - 0x30;
    }
    AVCodec *c = avcodec_find_decoder(codec->codec_id);
    if (c == NULL)
    {
      std::cerr << "Could not find decoder ID " << codec->codec_id << std::endl;
      throw - 0x31;
    }
    audioDecodec = avcodec_alloc_context3(c);
    if (audioDecodec == NULL)
    {
      std::cerr << "Could not alloc context for decoder " << c->name << std::endl;
      throw - 0x32;
    }
    avcodec_copy_context(audioDecodec, codec);
    int ret = avcodec_open2(audioDecodec, c, NULL);
    if (ret < 0)
    {
      std::cerr << "Could not open stream decoder " << c->name;
      throw - 0x33;
    }
  }
  const auto channels = audioDecodec->channels;
  // switch (audioDecodec->sample_fmt)
  // {
  // case AV_SAMPLE_FMT_NONE: std::cout << "sample_fmt: AV_SAMPLE_FMT_NONE" << std::endl; break;
  // case AV_SAMPLE_FMT_U8: std::cout << "sample_fmt: U8" << std::endl; break;
  // case AV_SAMPLE_FMT_S16: std::cout << "sample_fmt: S16" << std::endl; break;
  // case AV_SAMPLE_FMT_S32: std::cout << "sample_fmt: S32" << std::endl; break;
  // case AV_SAMPLE_FMT_FLT: std::cout << "sample_fmt: FLT" << std::endl; break;
  // case AV_SAMPLE_FMT_DBL: std::cout << "sample_fmt: DBL" << std::endl; break;
  // case AV_SAMPLE_FMT_U8P: std::cout << "sample_fmt: U8P" << std::endl; break;
  // case AV_SAMPLE_FMT_S16P: std::cout << "sample_fmt: S16P" << std::endl; break;
  // case AV_SAMPLE_FMT_S32P: std::cout << "sample_fmt: S32P" << std::endl; break;
  // case AV_SAMPLE_FMT_FLTP: std::cout << "sample_fmt: FLTP" << std::endl; break;
  // case AV_SAMPLE_FMT_DBLP: std::cout << "sample_fmt: DBLP" << std::endl; break;
  // case AV_SAMPLE_FMT_NB: std::cout << "sample_fmt: NB" << std::endl; break;
  // case AV_SAMPLE_FMT_S64: std::cout << "sample_fmt: S64" << std::endl; break;
  // case AV_SAMPLE_FMT_S64P: std::cout << "sample_fmt: S64P" << std::endl; break;
  // }
  AVPacket packet;
  while (av_read_frame(formatContext, &packet) == 0)
  {
    if (packet.stream_index == audioStreamIndex)
    {
      int gotFrame = 0;
      AVFrame *decodedFrame = av_frame_alloc();
      int len = avcodec_decode_audio4(audioDecodec, decodedFrame, &gotFrame, &packet);
      if (len >= 0)
      {
        if (gotFrame)
        {
          int dataSize = av_samples_get_buffer_size(
            nullptr, channels, decodedFrame->nb_samples, audioDecodec->sample_fmt, 1);
          if (audioDecodec->sample_fmt == AV_SAMPLE_FMT_FLT)
          {
            for (size_t i = 0; i < dataSize / sizeof(float) / channels; ++i)
            {
              int sum = 0;
              for (int c = 0; c < channels; ++c)
                sum += ((float *)decodedFrame->data[0])[i * channels + c] * 0x8000;
              audio.push_back(sum / channels);
            }
          }
          else if (audioDecodec->sample_fmt == AV_SAMPLE_FMT_FLTP)
          {
            for (size_t i = 0; i < dataSize / sizeof(float) / channels; ++i)
            {
              int sum = 0;
              for (int c = 0; c < channels; ++c)
                sum += ((float *)decodedFrame->data[c])[i] * 0x8000;
              audio.push_back(sum / channels);
            }
          }
          else if (audioDecodec->sample_fmt == AV_SAMPLE_FMT_S16)
          {
            for (size_t i = 0; i < dataSize / sizeof(int16_t) / channels; ++i)
            {
              int sum = 0;
              for (int c = 0; c < channels; ++c)
                sum += ((int16_t *)decodedFrame->data[0])[i * channels + c];
              audio.push_back(sum / channels);
            }
          }
          else if (audioDecodec->sample_fmt == AV_SAMPLE_FMT_S16P)
          {
            for (size_t i = 0; i < dataSize / sizeof(int16_t) / channels; ++i)
            {
              int sum = 0;
              for (int c = 0; c < channels; ++c)
                sum +=
                  ((int16_t *)decodedFrame->data[0])[i + c * dataSize / sizeof(int16_t) / channels];
              audio.push_back(sum / channels);
            }
          }
        }
      }
      av_free(decodedFrame);
    }
    av_packet_unref(&packet);
  }

  av_free(ctx);
#endif
  return audio;
}
