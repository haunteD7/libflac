#include <iostream>
#include <fstream>


int main(int argc, char *argv[])
{
    if(argc < 3) 
    {
        std::cerr << "Usage: flac <input> <output>\n";
        return -1;
    }
    char* file_in = argv[1];
    char* file_out = argv[2];

    std::ifstream fs_in(file_in, std::ios_base::binary);
    if(!fs_in.is_open())
    {
        std::cerr << "Can't open " << file_in << "\n";
        return -1;
    }
    std::ofstream fs_out(file_out, std::ios_base::binary);
    {
        std::cerr << "Can't open " << file_out << "\n";
        return -1;
    }

    return 0;
}