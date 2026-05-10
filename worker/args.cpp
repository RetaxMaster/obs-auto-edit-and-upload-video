#include "args.h"
#include <unordered_map>

WorkerArgs parse_args(int argc, char *argv[])
{
    std::unordered_map<std::string, std::string> kv;
    for (int i = 1; i + 1 < argc; i += 2)
        kv[argv[i]] = argv[i + 1];

    auto require = [&](const char *key) -> const std::string & {
        auto it = kv.find(key);
        if (it == kv.end())
            throw std::invalid_argument(std::string("Missing required argument: ") + key);
        return it->second;
    };

    WorkerArgs args;
    args.input      = require("--input");
    args.output     = require("--output");
    args.ffmpeg     = require("--ffmpeg");
    args.vcodec     = require("--vcodec");
    args.encoder    = require("--encoder");
    args.bitrate    = std::stoi(require("--bitrate"));
    args.progress   = require("--progress");
    args.out_width  = std::stoi(require("--width"));
    args.out_height = std::stoi(require("--height"));
    args.out_format = require("--format");

    auto it_intro = kv.find("--intro");
    if (it_intro != kv.end()) args.intro = it_intro->second;

    auto it_outro = kv.find("--outro");
    if (it_outro != kv.end()) args.outro = it_outro->second;

    return args;
}
