#include <iostream>
#include "fs.h"
#include <vector>
#include <string>
#include <cstring>
#include <sstream>

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
}

FS::~FS()
{
}

std::vector<std::string> splitPath(const std::string &path)
{
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string token;

    while (std::getline(ss, token, '/'))
    {
        if (!token.empty())
            parts.push_back(token);
    }
    return parts;
}

bool FS::resolvePath(const std::string &path,
                     int &parent_block,
                     std::string &name)
{
    int max = BLOCK_SIZE / sizeof(dir_entry);

    // 1. Tom path → fel
    if (path.empty())
        return false;

    // 2. Dela path i delar
    std::vector<std::string> parts = splitPath(path);

    // 3. Bestäm startpunkt
    int current = (path[0] == '/') ? ROOT_BLOCK : cwd_blk;

    // 4. Gå igenom alla delar UTOM sista
    for (int i = 0; i < (int)parts.size() - 1; i++)
    {
        if (parts[i] == "..")
        {
            // Gå till parent via ".."
            dir_entry dir[max];
            disk.read(current, (uint8_t *)dir);

            bool found = false;
            for (int j = 0; j < max; j++)
            {
                if (strcmp(dir[j].file_name, "..") == 0)
                {
                    current = dir[j].first_blk;
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        else
        {
            // Leta efter subdirectory
            dir_entry dir[max];
            disk.read(current, (uint8_t *)dir);

            bool found = false;
            for (int j = 0; j < max; j++)
            {
                if (dir[j].file_name[0] != '\0' &&
                    strcmp(dir[j].file_name, parts[i].c_str()) == 0 &&
                    dir[j].type == TYPE_DIR)
                {
                    current = dir[j].first_blk;
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
    }

    // 5. Returnera parent + sista namnet
    parent_block = current;
    name = parts.back();
    return true;
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

    cwd_blk = ROOT_BLOCK;
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

    // 1. Läs current directory

    dir_entry dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(cwd_blk, (uint8_t *)dir);

    // 2. Kolla om filen redan finns

    int max = BLOCK_SIZE / sizeof(dir_entry);
    for (int i = 0; i < max; i++)
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
    disk.write(cwd_blk, (uint8_t *)dir);
    disk.write(FAT_BLOCK, (uint8_t *)fat);

    // std::cout << "FS::create(" << filepath << ")\n";
    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string path)
{
    int max = BLOCK_SIZE / sizeof(dir_entry);

    int parent;
    std::string name;

    // 1. Resolve path
    if (!resolvePath(path, parent, name))
    {
        std::cout << "File not found\n";
        return -1;
    }

    // 2. Läs parent directory
    dir_entry dir[max];
    disk.read(parent, (uint8_t*)dir);

    // 3. Leta upp filen
    for (int i = 0; i < max; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            strcmp(dir[i].file_name, name.c_str()) == 0)
        {
            // 4. Kontrollera att det är en fil
            if (dir[i].type == TYPE_DIR)
            {
                std::cout << "Not a file\n";
                return -1;
            }

            // 5. Läs FAT
            disk.read(FAT_BLOCK, (uint8_t*)fat);

            int cur = dir[i].first_blk;
            int remaining = dir[i].size;

            // 6. Läs block för block
            while (cur != FAT_EOF && remaining > 0)
            {
                uint8_t buf[BLOCK_SIZE];
                disk.read(cur, buf);

                int n = std::min(BLOCK_SIZE, remaining);
                std::cout.write((char*)buf, n);

                remaining -= n;
                cur = fat[cur];
            }

            return 0;
        }
    }

    std::cout << "File not found\n";
    return -1;
}

// ls lists the content in the current directory (files and sub-directories)
int FS::ls()
{
    int max = BLOCK_SIZE / sizeof(dir_entry);

    dir_entry dir[max];
    disk.read(cwd_blk, (uint8_t *)dir);

    std::cout << "name     type    size\n";

    // 1. print directories first
    for (int i = 0; i < max; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            dir[i].type == TYPE_DIR)
        {
            // skip internal entries
            if (strcmp(dir[i].file_name, "..") == 0)
                continue;

            std::cout << dir[i].file_name
                      << "    dir     -\n";
        }
    }

    // 2. print files
    for (int i = 0; i < max; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            dir[i].type == TYPE_FILE)
        {
            std::cout << dir[i].file_name
                      << "    file    "
                      << dir[i].size << "\n";
        }
    }

    // std::cout << "FS::ls()\n";
    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string srcpath, std::string dstpath)
{
    int max = BLOCK_SIZE / sizeof(dir_entry);

    int src_parent;
    std::string src_name;

    // 1. Resolve source
    if (!resolvePath(srcpath, src_parent, src_name))
    {
        std::cout << "Source file not found\n";
        return -1;
    }

    dir_entry src_dir[max];
    disk.read(src_parent, (uint8_t*)src_dir);

    dir_entry *src = nullptr;
    for (int i = 0; i < max; i++)
    {
        if (src_dir[i].file_name[0] != '\0' &&
            strcmp(src_dir[i].file_name, src_name.c_str()) == 0)
        {
            src = &src_dir[i];
            break;
        }
    }

    if (!src || src->type == TYPE_DIR)
    {
        std::cout << "Source file not found\n";
        return -1;
    }

    // 2. Resolve destination
    int dst_parent;
    std::string dst_name;

    if (!resolvePath(dstpath, dst_parent, dst_name))
    {
        std::cout << "Destination not found\n";
        return -1;
    }

    dir_entry dst_dir[max];
    disk.read(dst_parent, (uint8_t*)dst_dir);

    // 3. If destination is an existing directory → copy inside it
    for (int i = 0; i < max; i++)
    {
        if (dst_dir[i].file_name[0] != '\0' &&
            strcmp(dst_dir[i].file_name, dst_name.c_str()) == 0 &&
            dst_dir[i].type == TYPE_DIR)
        {
            dst_parent = dst_dir[i].first_blk;
            disk.read(dst_parent, (uint8_t*)dst_dir);
            dst_name = src_name;
            break;
        }
    }

    // 4. Check noclobber
    for (int i = 0; i < max; i++)
    {
        if (dst_dir[i].file_name[0] != '\0' &&
            strcmp(dst_dir[i].file_name, dst_name.c_str()) == 0)
        {
            std::cout << "File already exists\n";
            return -1;
        }
    }

    // 5. Find free entry in destination directory
    int free_idx = -1;
    for (int i = 0; i < max; i++)
    {
        if (dst_dir[i].file_name[0] == '\0')
        {
            free_idx = i;
            break;
        }
    }

    if (free_idx == -1)
    {
        std::cout << "Directory full\n";
        return -1;
    }

    // 6. Read FAT
    disk.read(FAT_BLOCK, (uint8_t*)fat);

    int size = src->size;
    int blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    std::vector<int> blocks;
    for (int i = 2; i < disk.get_no_blocks() && blocks.size() < blocks_needed; i++)
    {
        if (fat[i] == FAT_FREE)
            blocks.push_back(i);
    }

    if (blocks.size() < blocks_needed)
    {
        std::cout << "Not enough disk space\n";
        return -1;
    }

    // 7. Copy data
    int cur = src->first_blk;
    for (int i = 0; i < blocks.size(); i++)
    {
        uint8_t buf[BLOCK_SIZE] = {0};
        disk.read(cur, buf);
        disk.write(blocks[i], buf);
        fat[blocks[i]] = (i + 1 < blocks.size()) ? blocks[i + 1] : FAT_EOF;
        cur = fat[cur];
    }

    // 8. Create new directory entry
    strncpy(dst_dir[free_idx].file_name, dst_name.c_str(), 55);
    dst_dir[free_idx].file_name[55] = '\0';
    dst_dir[free_idx].type = TYPE_FILE;
    dst_dir[free_idx].size = size;
    dst_dir[free_idx].first_blk = blocks[0];

    // 9. Write back
    disk.write(dst_parent, (uint8_t*)dst_dir);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string srcpath, std::string dstpath)
{
    int max = BLOCK_SIZE / sizeof(dir_entry);

    /* ---------- resolve source ---------- */
    int src_parent;
    std::string src_name;

    if (!resolvePath(srcpath, src_parent, src_name))
    {
        std::cout << "File not found\n";
        return -1;
    }

    dir_entry src_dir[max];
    disk.read(src_parent, (uint8_t*)src_dir);

    int src_idx = -1;
    for (int i = 0; i < max; i++)
    {
        if (src_dir[i].file_name[0] != '\0' &&
            strcmp(src_dir[i].file_name, src_name.c_str()) == 0)
        {
            src_idx = i;
            break;
        }
    }

    if (src_idx == -1)
    {
        std::cout << "File not found\n";
        return -1;
    }

    if (src_dir[src_idx].type == TYPE_DIR)
    {
        std::cout << "Cannot move directory\n";
        return -1;
    }

    /* ---------- resolve destination ---------- */
    int dst_parent;
    std::string dst_name;

    if (!resolvePath(dstpath, dst_parent, dst_name))
    {
        std::cout << "Destination not found\n";
        return -1;
    }

    dir_entry dst_dir[max];
    disk.read(dst_parent, (uint8_t*)dst_dir);

    /* ---------- check if destination is a directory ---------- */
    for (int i = 0; i < max; i++)
    {
        if (dst_dir[i].file_name[0] != '\0' &&
            strcmp(dst_dir[i].file_name, dst_name.c_str()) == 0 &&
            dst_dir[i].type == TYPE_DIR)
        {
            // flytt till katalog
            dst_parent = dst_dir[i].first_blk;
            disk.read(dst_parent, (uint8_t*)dst_dir);
            dst_name = src_name;
            break;
        }
    }

    /* ---------- noclobber ---------- */
    for (int i = 0; i < max; i++)
    {
        if (dst_dir[i].file_name[0] != '\0' &&
            strcmp(dst_dir[i].file_name, dst_name.c_str()) == 0)
        {
            std::cout << "File already exists\n";
            return -1;
        }
    }

    /* ---------- hitta ledig plats i destination ---------- */
    int free_idx = -1;
    for (int i = 0; i < max; i++)
    {
        if (dst_dir[i].file_name[0] == '\0')
        {
            free_idx = i;
            break;
        }
    }

    if (free_idx == -1)
    {
        std::cout << "Directory full\n";
        return -1;
    }

    /* ---------- flytta directory entry ---------- */
    dst_dir[free_idx] = src_dir[src_idx];

    strncpy(dst_dir[free_idx].file_name, dst_name.c_str(), 55);
    dst_dir[free_idx].file_name[55] = '\0';

    memset(&src_dir[src_idx], 0, sizeof(dir_entry));

    /* ---------- write back ---------- */
    disk.write(src_parent, (uint8_t*)src_dir);
    disk.write(dst_parent, (uint8_t*)dst_dir);

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string path)
{
    int max = BLOCK_SIZE / sizeof(dir_entry);

    int parent;
    std::string name;

    // 1. Resolve path
    if (!resolvePath(path, parent, name))
    {
        std::cout << "File not found\n";
        return -1;
    }

    // 2. Läs parent directory
    dir_entry dir[max];
    disk.read(parent, (uint8_t*)dir);

    // 3. Hitta entry
    int idx = -1;
    for (int i = 0; i < max; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            strcmp(dir[i].file_name, name.c_str()) == 0)
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

    // 4. Om katalog: kontrollera att den är tom
    if (dir[idx].type == TYPE_DIR)
    {
        dir_entry sub[max];
        disk.read(dir[idx].first_blk, (uint8_t*)sub);

        for (int i = 0; i < max; i++)
        {
            if (sub[i].file_name[0] != '\0' &&
                strcmp(sub[i].file_name, "..") != 0)
            {
                std::cout << "Directory not empty\n";
                return -1;
            }
        }
    }

    // 5. Läs FAT och frigör block
    disk.read(FAT_BLOCK, (uint8_t*)fat);

    int cur = dir[idx].first_blk;
    while (cur != FAT_EOF)
    {
        int next = fat[cur];
        fat[cur] = FAT_FREE;
        cur = next;
    }

    // 6. Rensa directory entry
    memset(&dir[idx], 0, sizeof(dir_entry));

    // 7. Skriv tillbaka
    disk.write(parent, (uint8_t*)dir);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

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
    int max = BLOCK_SIZE / sizeof(dir_entry);

    // 1. Resolve path
    int parent;
    std::string name;

    if (!resolvePath(dirpath, parent, name))
    {
        std::cout << "Directory not found\n";
        return -1;
    }

    if (name.length() > 55)
    {
        std::cout << "Directory name too long\n";
        return -1;
    }

    // 2. Läs parent directory
    dir_entry dir[max]; 
    disk.read(parent, (uint8_t *)dir);

    // 2. check if name already exists
    for (int i = 0; i < max; i++)
    {
        if (strcmp(dir[i].file_name, name.c_str()) == 0)
        {
            if (dir[i].type == TYPE_DIR)
                std::cout << "Directory already exists\n";
            else
                std::cout << "File with same name exists\n";
            return -1;
        }
    }

    // 3. find free directory entry
    int free_idx = -1;
    for (int i = 0; i < max; i++)
    {
        if (dir[i].file_name[0] == '\0')
        {
            free_idx = i;
            break;
        }
    }

    if (free_idx == -1)
    {
        std::cout << "Directory full\n";
        return -1;
    }

    // 4. read FAT
    disk.read(FAT_BLOCK, (uint8_t *)fat);

    // 5. find free block
    int new_blk = -1;
    for (int i = 2; i < disk.get_no_blocks(); i++)
    {
        if (fat[i] == FAT_FREE)
        {
            new_blk = i;
            fat[i] = FAT_EOF;
            break;
        }
    }

    if (new_blk == -1)
    {
        std::cout << "No free blocks\n";
        return -1;
    }

    // 6. create new directory block
    dir_entry newdir[BLOCK_SIZE / sizeof(dir_entry)];
    memset(newdir, 0, sizeof(newdir));

    strncpy(newdir[0].file_name, "..", 55);
    newdir[0].file_name[55] = '\0';
    newdir[0].type = TYPE_DIR;
    newdir[0].first_blk = parent;

    disk.write(new_blk, (uint8_t *)newdir);

    // 7. add directory entry to current directory
    strncpy(dir[free_idx].file_name, name.c_str(), 55);
    dir[free_idx].file_name[55] = '\0';
    dir[free_idx].type = TYPE_DIR;
    dir[free_idx].first_blk = new_blk;

    // 8. write back
    disk.write(parent, (uint8_t *)dir);
    disk.write(FAT_BLOCK, (uint8_t *)fat);

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string path)
{
    int max = BLOCK_SIZE / sizeof(dir_entry);

    int parent;
    std::string name;

    // 1. Resolve path
    if (!resolvePath(path, parent, name))
    {
        std::cout << "Directory not found\n";
        return -1;
    }

    // 2. Läs parent directory
    dir_entry dir[max];
    disk.read(parent, (uint8_t*)dir);

    // 3. Leta upp målet
    for (int i = 0; i < max; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            strcmp(dir[i].file_name, name.c_str()) == 0)
        {
            if (dir[i].type != TYPE_DIR)
            {
                std::cout << "Not a directory\n";
                return -1;
            }

            // 4. Uppdatera current directory
            cwd_blk = dir[i].first_blk;
            return 0;
        }
    }

    std::cout << "Directory not found\n";
    return -1;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
    int max = BLOCK_SIZE / sizeof(dir_entry);

    // Specialfall: root
    if (cwd_blk == ROOT_BLOCK)
    {
        std::cout << "/\n";
        return 0;
    }

    std::vector<std::string> path;
    int current = cwd_blk;

    while (current != ROOT_BLOCK)
    {
        dir_entry dir[max];
        disk.read(current, (uint8_t*)dir);

        int parent = -1;

        // hitta ".."
        for (int i = 0; i < max; i++)
        {
            if (strcmp(dir[i].file_name, "..") == 0)
            {
                parent = dir[i].first_blk;
                break;
            }
        }

        if (parent == -1)
            break;

        // leta upp current i parent för att få namnet
        dir_entry parent_dir[max];
        disk.read(parent, (uint8_t*)parent_dir);

        for (int i = 0; i < max; i++)
        {
            if (parent_dir[i].type == TYPE_DIR &&
                parent_dir[i].first_blk == current)
            {
                path.push_back(parent_dir[i].file_name);
                break;
            }
        }

        current = parent;
    }

    // skriv ut path baklänges
    std::cout << "/";
    for (int i = path.size() - 1; i >= 0; i--)
    {
        std::cout << path[i];
        if (i > 0)
            std::cout << "/";
    }
    std::cout << "\n";

    return 0;
}


// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
    int max = BLOCK_SIZE / sizeof(dir_entry);

    // 1. konvertera accessrights till int
    int rights;
    try
    {
        rights = std::stoi(accessrights);
    }
    catch (...)
    {
        std::cout << "Invalid access rights\n";
        return -1;
    }

    if (rights < 0 || rights > 7)
    {
        std::cout << "Invalid access rights\n";
        return -1;
    }

    // 2. resolve path
    int parent;
    std::string name;

    if (!resolvePath(filepath, parent, name))
    {
        std::cout << "File not found\n";
        return -1;
    }

    // 3. läs parent directory
    dir_entry dir[max];
    disk.read(parent, (uint8_t*)dir);

    // 4. hitta entry
    for (int i = 0; i < max; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            strcmp(dir[i].file_name, name.c_str()) == 0)
        {
            dir[i].access_rights = rights;
            disk.write(parent, (uint8_t*)dir);
            return 0;
        }
    }

    std::cout << "File not found\n";
    return -1;
}
