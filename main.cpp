#include "base64.hpp"
#include "decode_audio.hpp"
#include <cstring>
#include <curl/curl.h>
#include <iostream>
#include <memory>
#include <sdlpp/sdlpp.hpp>
#include <sstream>
#include <stdexcept>

static size_t curlWrite(void *buffer, size_t size, size_t nmemb, void *userp)
{
  ((std::ostringstream *)userp)->write((char *)buffer, size * nmemb);
  return size * nmemb;
}

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    std::cerr << "Need text\n";
    return -1;
  }

  const std::string text = argv[1];

  if (curl_global_init(CURL_GLOBAL_ALL))
    throw std::runtime_error("Unable to curl_global_init");

  // run curl
  // make json
  std::ostringstream strm;
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> tmp(curl_easy_init(), curl_easy_cleanup);
  if (!tmp)
    throw std::runtime_error("Failed to init");
  auto handle = tmp.get();
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curlWrite);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &strm);
  curl_easy_setopt(handle, CURLOPT_URL, "https://texttospeech.googleapis.com/v1/text:synthesize");
  struct curl_slist *chunk = NULL;
  chunk = curl_slist_append(
    chunk,
    "Authorization: Bearer "
    "ya29.Il-zB4gJOKmmr3KyStsQw8nd9D994EXCwd5aWnofHdwOpB5EgXjRUx1wFdXBDgiemoLEVogO19ng4ysTe8No6Poq-"
    "2akjEoEu3rbq0hCDYhRtJWFHZO0mtvA9lclHJKBkg");
  chunk = curl_slist_append(chunk, "Content-Type: application/json; charset=utf-8");
  curl_easy_setopt(handle, CURLOPT_HTTPHEADER, chunk);
  long noSignal = 1;
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, noSignal);
  std::string json = R"({
    'input':{
      'text':')" + text +
                     R"('
    },
    'voice':{
      'languageCode':'en-US',
      'name':'en-US-Wavenet-C',
      'ssmlGender':'FEMALE'
    },
    'audioConfig':{
      'audioEncoding':'OGG_OPUS'
    }
  })";
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, json.c_str());
  CURLcode code = curl_easy_perform(handle);
  strm.flush();
  if (code != CURLE_OK)
  {
    std::ostringstream strm;
    strm << "Could not download. CURL finished with error " << code << ": "
         << curl_easy_strerror(code);
    throw std::runtime_error(strm.str());
  }

  // parse json
  std::istringstream istrm(strm.str());
  std::string line;
  std::string base64;
  while (std::getline(istrm, line))
  {
    const auto pos = line.find("\"audioContent\": \"");
    if (pos != std::string::npos)
    {
      base64 = line.substr(pos + strlen("\"audioContent\": \""));
      base64.resize(base64.size() - 1);
      break;
    }
  }
  if (base64.empty())
  {
    std::cerr << strm.str() << std::endl;
    return -1;
  }
  auto const compressedAudio = base64_decode(base64);
  // decode content
  auto wav = decodeAudio(compressedAudio);

  // play on speakers
  try
  {
    sdl::Init init(SDL_INIT_EVERYTHING);
    SDL_AudioSpec want, have;
    want.freq = 48000;
    want.format = AUDIO_S16;
    want.channels = 1;
    want.samples = 4096;
    auto time = 0u;
    auto done = false;
    sdl::Audio audio(nullptr, false, &want, &have, 0, [&wav, &time, &done](Uint8 *stream, int len) {
      int16_t *s = (int16_t *)stream;
      for (auto i = 0u; i < len / sizeof(int16_t); ++i, ++time, ++s)
      {
        if (time < wav.size())
          *s = wav[time];
        else
        {
          *s = 0;
          done = true;
        }
      }
      return len;
    });
    audio.pause(false);
    sdl::EventHandler e;
    e.quit = [&done](const SDL_QuitEvent &) { done = true; };
    while (!done)
    {
      e.poll();
    }
  }
  catch (sdl::Error &e)
  {
    // error handling
  }

  curl_global_cleanup();
}
