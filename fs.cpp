#include <iostream>
#include "fs.h"
#include <vector>
#include <string>
#include <cstring>
FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
}

FS::~FS()
{
}

// formats the disk, i.e., creates an empty file system
int FS::format()
{
    // 1. Markera alla FAT-poster som fria
    for (int i = 0; i < BLOCK_SIZE / 2; i++)
    {
        fat[i] = FAT_FREE;
    }

    // 2. Reservera root (0) och FAT (1)
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;

    // 3. Skriv FAT till disk (block 1)
    disk.write(FAT_BLOCK, (uint8_t *)fat);

    // 4. Skapa tom root directory
    uint8_t empty_dir[BLOCK_SIZE] = {0};

    // 5. Skriv root directory till disk (block 0)
    disk.write(ROOT_BLOCK, empty_dir);

    // std::cout << "FS::format()\n";
    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
    // 1. Läs root directory
    dir_entry dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, (uint8_t *)dir);

    // 2. Kolla om filen redan finns
    for (int i = 0; i < 64; i++)
    {
        if (strcmp(dir[i].file_name, filepath.c_str()) == 0)
        {
            std::cout << "File already exists\n";
            return -1;
        }
    }

    // 3. Hitta en ledig post i root directory
    int free_index = -1;
    for (int i = 0; i < 64; i++)
    {
        if (dir[i].file_name[0] == '\0')
        {
            free_index = i;
            break;
        }
    }

    if (free_index == -1)
    {
        std::cout << "Directory full\n";
        return -1;
    }

    // 4. Läs in data från stdin
    std::string data, line;
    while (std::getline(std::cin, line))
    {
        if (line.empty())
            break;
        data += line + "\n";
    }

    // 5. Räkna ut antal block som behövs
    int size = data.size();
    int blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // 6. läs FAT
    disk.read(FAT_BLOCK, (uint8_t *)fat);

    // 7. hitta lediga block i FAT
    std::vector<int> blocks;
    for (int i = 2; i < BLOCK_SIZE / 2 && blocks.size() < blocks_needed; i++)
    {
        if (fat[i] == FAT_FREE)
            blocks.push_back(i);
    }

    // 8. Kolla om det finns tillräckligt med lediga block
    if (blocks.size() < blocks_needed)
    {
        std::cout << "Not enough disk space\n";
        return -1;
    }

    // 9. länka ihop blocken i FAT
    for (int i = 0; i < blocks.size() - 1; i++)
        fat[blocks[i]] = blocks[i + 1];

    fat[blocks.back()] = FAT_EOF;

    // 10. Skriv data till disken
    for (int i = 0; i < blocks.size(); i++)
    {
        uint8_t buf[BLOCK_SIZE] = {0};
        memcpy(buf, 
               data.data() + i * BLOCK_SIZE,
               std::min(BLOCK_SIZE, size - i * BLOCK_SIZE));
        disk.write(blocks[i], buf);
    }

    // 11. skapa directory entry
    strncpy(dir[free_index].file_name, filepath.c_str(), 55);
    dir[free_index].file_name[55] = '\0';
    dir[free_index].size = size;
    dir[free_index].first_blk = blocks[0];
    dir[free_index].type = TYPE_FILE;

    // 12. Skriv tillbaka FAT och root directory till disken
    disk.write(ROOT_BLOCK, (uint8_t *)dir);
    disk.write(FAT_BLOCK, (uint8_t *)fat);

    // std::cout << "FS::create(" << filepath << ")\n";
    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{   

    // std::cout << "FS::cat(" << filepath << ")\n";
    return 0;
}

// ls lists the content in the current directory (files and sub-directories)
int FS::ls()
{
    std::cout << "FS::ls()\n";
    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";
    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
    std::cout << "FS::pwd()\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}
