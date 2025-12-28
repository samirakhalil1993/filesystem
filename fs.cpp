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

//  create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
    if (filepath.length() > 55)
    {
        std::cout << "File name too long\n";
        return -1;
    }

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
    // 1. Läs root directory
    dir_entry dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, (uint8_t *)dir);

    // 2. Hitta filen
    int idx = -1;
    for (int i = 0; i < 64; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            strcmp(dir[i].file_name, filepath.c_str()) == 0)
        {
            idx = i;
            break;
        }
    }

    if (idx == -1)
    {
        std::cout << "File not found\n";
        return -1;
    }

    // 3. Läs FAT
    disk.read(FAT_BLOCK, (uint8_t *)fat);

    int cur = dir[idx].first_blk;
    int remaining = dir[idx].size;

    // 4. Läs block för block och följ FAT-kedjan
    while (cur != FAT_EOF && remaining > 0)
    {
        uint8_t buf[BLOCK_SIZE];
        disk.read(cur, buf);

        int n = std::min(BLOCK_SIZE, remaining);
        std::cout.write((char *)buf, n);

        remaining -= n;
        cur = fat[cur]; // hoppa till nästa block i kedjan
    }

    // std::cout << "FS::cat(" << filepath << ")\n";
    return 0;
}

// ls lists the content in the current directory (files and sub-directories)
int FS::ls()
{
    // 1. Läs root directory
    dir_entry dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, (uint8_t *)dir);

    // 2.Skriv rubriken
    std::cout << "name\t size\n";

    // 3. Loopa igenom alla directory-entries
    for (int i = 0; i < 64; i++)
    {
        if (dir[i].file_name[0] != '\0')
        {
            std::cout << dir[i].file_name << "\t "
                      << dir[i].size << "\n";
        }
    }

    // std::cout << "FS::ls()\n";
    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string sourcepath, std::string destpath)
{
    // 1. Läs root directory
    dir_entry dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, (uint8_t *)dir);

    // 2. hitta source-filen
    int src = -1;
    for (int i = 0; i < 64; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            strcmp(dir[i].file_name, sourcepath.c_str()) == 0)
        {
            src = i;
            break;
        }
    }

    if (src == -1)
    {
        std::cout << "Source file not found\n";
        return -1;
    }

    // 3. kolla att dest INTE finns.
    for (int i = 0; i < 64; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            strcmp(dir[i].file_name, destpath.c_str()) == 0)
        {
            std::cout << "Destination already exists\n";
            return -1;
        }
    }
    // 4. hitta en ledig post i root directory
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
    // 5. läs FAT
    disk.read(FAT_BLOCK, (uint8_t *)fat);

    // 6. läs in source-filens data
    int size = dir[src].size;
    std::vector<uint8_t> data(size);

    int current = dir[src].first_blk;
    int offset = 0;

    while (current != FAT_EOF)
    {
        uint8_t buf[BLOCK_SIZE];
        disk.read(current, buf);

        int n = std::min(BLOCK_SIZE, size - offset);
        memcpy(&data[offset], buf, n);

        offset += n;
        current = fat[current];
    }
    // 7. räkna ut antal block som behövs
    int blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // 8. hitta lediga block i FAT
    std::vector<int> blocks;
    for (int i = 2; i < BLOCK_SIZE / 2 && blocks.size() < blocks_needed; i++)
    {
        if (fat[i] == FAT_FREE)
            blocks.push_back(i);
    }

    if (blocks.size() < blocks_needed)
    {
        std::cout << "Not enough disk space\n";
        return -1;
    }
    // 9. länka FAT-kedjan
    for (int i = 0; i < blocks.size() - 1; i++)
        fat[blocks[i]] = blocks[i + 1];

    fat[blocks.back()] = FAT_EOF;

    // 10. skriv data till nya block
    for (int i = 0; i < blocks.size(); i++)
    {
        uint8_t buf[BLOCK_SIZE] = {0};
        memcpy(buf, &data[i * BLOCK_SIZE],
               std::min(BLOCK_SIZE, size - i * BLOCK_SIZE));
        disk.write(blocks[i], buf);
    }

    // 11. skapa directory entry för nya filen
    strncpy(dir[free_index].file_name, destpath.c_str(), 55);
    dir[free_index].file_name[55] = '\0';
    dir[free_index].size = size;
    dir[free_index].first_blk = blocks[0];
    dir[free_index].type = TYPE_FILE;

    // 12. skriv tillbaka FAT och root directory till disken
    disk.write(ROOT_BLOCK, (uint8_t *)dir);
    disk.write(FAT_BLOCK, (uint8_t *)fat);

    // std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
    // 1) Read root directory
    dir_entry dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, (uint8_t *)dir);

    // 2) Find source
    int src = -1;
    for (int i = 0; i < 64; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            strcmp(dir[i].file_name, sourcepath.c_str()) == 0)
        {
            src = i;
            break;
        }
    }
    if (src == -1)
    {
        std::cout << "File not found\n";
        return -1;
    }

    // 3) Check noclobber: dest must NOT exist
    for (int i = 0; i < 64; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            strcmp(dir[i].file_name, destpath.c_str()) == 0)
        {
            std::cout << "File already exists\n";
            return -2;
        }
    }

    // 4) Rename (metadata only)
    strncpy(dir[src].file_name, destpath.c_str(), 55);
    dir[src].file_name[55] = '\0';

    // 5) Write back directory
    disk.write(ROOT_BLOCK, (uint8_t *)dir);
    // std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
    // 1) Läs root directory
    dir_entry dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, (uint8_t *)dir);

    // 2) Hitta filen
    int idx = -1;
    for (int i = 0; i < 64; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            strcmp(dir[i].file_name, filepath.c_str()) == 0)
        {
            idx = i;
            break;
        }
    }
    if (idx == -1)
    {
        std::cout << "File not found\n";
        return -1;
    }

    // 3) Läs FAT och frigör block
    disk.read(FAT_BLOCK, (uint8_t *)fat);

    int cur = dir[idx].first_blk;
    while (cur != FAT_EOF)
    {
        int next = fat[cur];
        fat[cur] = FAT_FREE;
        cur = next;
    }

    // 4) Rensa directory entry
    memset(&dir[idx], 0, sizeof(dir_entry));

    // 5) Skriv tillbaka FAT och root directory till disken
    disk.write(ROOT_BLOCK, (uint8_t *)dir);
    disk.write(FAT_BLOCK, (uint8_t *)fat);
    // std::cout << "FS::rm(" << filepath << ")\n";
    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
    // 1. read root directory
    dir_entry dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(ROOT_BLOCK, (uint8_t *)dir);

    // 2. find file 1 and file 2
    int src = -1, dst = -1;
    for (int i = 0; i < 64; i++)
    {
        if (dir[i].file_name[0] != '\0')
        {
            if (strcmp(dir[i].file_name, filepath1.c_str()) == 0)
                src = i;
            if (strcmp(dir[i].file_name, filepath2.c_str()) == 0)
                dst = i;
        }
    }
    if (src == -1 || dst == -1)
    {
        std::cout << "File not found\n";
        return -1;
    }

    // 3. read FAT
    disk.read(FAT_BLOCK, (uint8_t *)fat);

    // 4. read data from file 1
    int size1 = dir[src].size;
    std::vector<uint8_t> data1(size1);
    int current = dir[src].first_blk;
    int offset = 0;
    while (current != FAT_EOF)
    {
        uint8_t buf[BLOCK_SIZE];
        disk.read(current, buf);

        int n = std::min(BLOCK_SIZE, size1 - offset);
        memcpy(&data1[offset], buf, n);

        offset += n;
        current = fat[current];
    }
    // 5. find end of file 2's block
    int last = dir[dst].first_blk;
    while (fat[last] != FAT_EOF)
    {
        last = fat[last];
    }

    // 6. read last block of destination file
    uint8_t last_buf[BLOCK_SIZE];
    disk.read(last, last_buf);

    // 7. calculate offset in last block
    int offset2 = dir[dst].size % BLOCK_SIZE;

    // 8. write as much as possible into last block
    int written = std::min(BLOCK_SIZE - offset2, size1);
    memcpy(last_buf + offset2, data1.data(), written);

    // 9. write last block back to disk
    disk.write(last, last_buf);

    int remaining = size1 - written;
    int pos = written;

    while (remaining > 0)
    {
        // find free block
        int new_blk = -1;
        for (int i = 2; i < BLOCK_SIZE / 2; i++)
        {
            if (fat[i] == FAT_FREE)
            {
                new_blk = i;
                break;
            }
        }

        if (new_blk == -1)
        {
            std::cout << "Not enough disk space\n";
            return -1;
        }

        // link FAT
        fat[last] = new_blk;
        fat[new_blk] = FAT_EOF;
        last = new_blk;

        // write data
        uint8_t buf[BLOCK_SIZE] = {0};
        int n = std::min(BLOCK_SIZE, remaining);
        memcpy(buf, data1.data() + pos, n);
        disk.write(new_blk, buf);

        pos += n;
        remaining -= n;
    }
    
    // 10. update directory entry for file 2
    dir[dst].size += size1;

    // 11. write back FAT and root directory to disk
    disk.write(FAT_BLOCK, (uint8_t *)fat);
    disk.write(ROOT_BLOCK, (uint8_t *)dir);

    // std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";
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
