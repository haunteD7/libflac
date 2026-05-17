#include <decoder.h>

#include <iostream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char *argv[])
{
#ifndef NDEBUG
    fs::path f_in = "in.flac";
    fs::path f_out = "out.wav";
#else
    if(argc < 3) 
    {
        std::cerr << "Usage: flac <input> <output>\n";
        return -1;
    }
    fs::path f_in = argv[1];
    fs::path f_out = argv[2];
#endif
    
    if(f_out.extension() == ".flac")
    {
        std::cerr << "Convertion to FLAC is not implemented yet\n";
        return -1;
    }
    else if(f_out.extension() == ".wav")
    {
        if(f_in.extension() == ".flac")
        {
            std::vector<uint8_t> out(4 * fs::file_size(f_in));
            FlacDecoder f(out);
            size_t final_size = f.decode(f_in);
            std::ofstream of(f_out, std::ios::binary);
            if(!of.is_open())
            {
                std::cerr << "Can't open output file " << f_out.filename() << "\n";
                return -1;
            }
            of.write(reinterpret_cast<const char*>(out.data()), final_size);
        }
        else
        {
            std::cerr << "Unsupported output extention\n";
            return -1;
        }
    }
    else
    {
        std::cerr << "Unsupported input extention\n";
        return -1;
    }

    return 0;
}