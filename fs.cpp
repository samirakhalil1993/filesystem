#include <iostream>
#include "fs.h"
#include <vector>
#include <string>
#include <cstring>
#include <sstream>

// ls lists the content in the current directory (files and sub-directories)
#include <iomanip> // högst upp i filen

// FS implementation notes:
// - Directory entries live in one block; max entries = BLOCK_SIZE / sizeof(dir_entry)
// - Names are stored in dir_entry::file_name with max length 55 (+ '\0')
// - FAT uses 16-bit entries; FAT_FREE and FAT_EOF mark free/end-of-chain

static constexpr int MAX_NAME_LEN = 55;
static constexpr int MAX_DIR_ENTRIES = BLOCK_SIZE / sizeof(dir_entry);

static int findEntryIndex(dir_entry *dir, int max, const std::string &name)
{
    for (int i = 0; i < max; i++)
    {
        if (dir[i].file_name[0] != '\0' &&
            std::strcmp(dir[i].file_name, name.c_str()) == 0)
        {
            return i;
        }
    }
    return -1;
}

static int findFreeIndex(dir_entry *dir, int max)
{
    for (int i = 0; i < max; i++)
    {
        if (dir[i].file_name[0] == '\0')
            return i;
    }
    return -1;
}

static bool isFile(const dir_entry &e)
{
    return e.type == TYPE_FILE;
}

static bool isDir(const dir_entry &e)
{
    return e.type == TYPE_DIR;
}

// Helper function that splits a file system path into individual directory or file names.
// The path is separated using '/' and empty components are ignored.

static std::vector<std::string> splitPath(const std::string &path)
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

// Resolves a given absolute or relative path by traversing the file system.
// On success, it returns the block number of the parent directory and the
// final file or directory name. Returns false if the path is invalid.

bool FS::resolvePath(const std::string &path,
                     int &parent_block,
                     std::string &name)
{
    if (path.empty())
        return false;

    std::vector<std::string> parts = splitPath(path);
    if (parts.empty())
        return false; // keeps behavior safe for "/" cases

    int current = (path[0] == '/') ? ROOT_BLOCK : cwd_blk;

    // Traverse all components except the last => find the parent directory block
    for (int i = 0; i < (int)parts.size() - 1; i++)
    {
        dir_entry dir[MAX_DIR_ENTRIES];
        disk.read(current, (uint8_t *)dir);

        if (parts[i] == "..")
        {
            int idx = findEntryIndex(dir, MAX_DIR_ENTRIES, "..");
            if (idx == -1)
                return false;

            current = dir[idx].first_blk;
            continue;
        }

        int idx = findEntryIndex(dir, MAX_DIR_ENTRIES, parts[i]);
        if (idx == -1 || dir[idx].type != TYPE_DIR)
            return false;

        current = dir[idx].first_blk;
    }

    parent_block = current;
    name = parts.back();
    return true;
}

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

    for (int i = 0; i < BLOCK_SIZE / 2; i++)
    {
        fat[i] = FAT_FREE;
    }

    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;

    disk.write(FAT_BLOCK, (uint8_t *)fat);
    uint8_t empty_dir[BLOCK_SIZE] = {0};
    disk.write(ROOT_BLOCK, empty_dir);
    cwd_blk = ROOT_BLOCK;

    return 0;
}

//  create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
    if (filepath.length() > MAX_NAME_LEN)
    {
        std::cout << "File name too long\n";
        return -1;
    }

    dir_entry dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(cwd_blk, (uint8_t *)dir);

    if (findEntryIndex(dir, MAX_DIR_ENTRIES, filepath) != -1)
    {
        std::cout << "File already exists\n";
        return -1;
    }

    int free_index = findFreeIndex(dir, MAX_DIR_ENTRIES);
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
    for (int i = 2; i < disk.get_no_blocks() && (int)blocks.size() < blocks_needed; i++)

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
    strncpy(dir[free_index].file_name, filepath.c_str(), MAX_NAME_LEN);
    dir[free_index].file_name[MAX_NAME_LEN] = '\0';
    dir[free_index].size = size;
    dir[free_index].first_blk = blocks[0];
    dir[free_index].type = TYPE_FILE;
    dir[free_index].access_rights = READ | WRITE; // 0x06

    // 12. Skriv tillbaka FAT och root directory till disken
    disk.write(cwd_blk, (uint8_t *)dir);
    disk.write(FAT_BLOCK, (uint8_t *)fat);

    // std::cout << "FS::create(" << filepath << ")\n";
    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string path)
{

    int parent;
    std::string name;

    // 1) Resolve path -> parent directory block + entry name
    if (!resolvePath(path, parent, name))
    {
        std::cout << "File not found\n";
        return -1;
    }

    // 2) Read parent directory
    dir_entry dir[MAX_DIR_ENTRIES];
    disk.read(parent, (uint8_t *)dir);

    // 3) Find entry
    int idx = findEntryIndex(dir, MAX_DIR_ENTRIES, name);
    if (idx == -1)
    {
        std::cout << "File not found\n";
        return -1;
    }

    dir_entry &entry = dir[idx];

    // 4) Must be a file
    if (entry.type == TYPE_DIR)
    {
        std::cout << "Not a file\n";
        return -1;
    }

    // 5) Check READ permission
    if (!(entry.access_rights & READ))
    {
        std::cout << "Permission denied\n";
        return -1;
    }

    // 6) Read FAT
    disk.read(FAT_BLOCK, (uint8_t *)fat);

    int cur = entry.first_blk;
    int remaining = entry.size;

    // 7) Read blocks following FAT chain
    while (cur != FAT_EOF && remaining > 0)
    {
        uint8_t buf[BLOCK_SIZE];
        disk.read(cur, buf);

        int n = std::min(BLOCK_SIZE, remaining);
        std::cout.write((char *)buf, n);

        remaining -= n;
        cur = fat[cur];
    }

    return 0;
}

int FS::ls()
{
    dir_entry dir[MAX_DIR_ENTRIES];
    disk.read(cwd_blk, (uint8_t *)dir);

    std::cout << std::left
              << std::setw(17) << "name"
              << std::setw(11) << "type"
              << std::setw(16) << "accessrights"
              << "size\n";

    for (int i = 0; i < MAX_DIR_ENTRIES; i++)
    {
        if (dir[i].file_name[0] == '\0')
            continue;

        uint8_t r = dir[i].access_rights;

        std::string rights;
        if (dir[i].type == TYPE_FILE)
        {
            rights += (r & READ) ? 'r' : '-';
            rights += (r & WRITE) ? 'w' : '-';
            rights += '-';
        }
        else
        {
            rights += (r & READ) ? 'r' : '-';
            rights += (r & WRITE) ? 'w' : '-';
            rights += (r & EXECUTE) ? 'x' : '-';
        }

        std::cout << std::left
                  << std::setw(17) << dir[i].file_name
                  << std::setw(11) << (dir[i].type == TYPE_DIR ? "dir" : "file")
                  << std::setw(16) << rights;

        if (dir[i].type == TYPE_DIR)
            std::cout << "-\n";
        else

            std::cout << dir[i].size << "\n";
    }

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string srcpath, std::string dstpath)
{

    // ---------- 1) Resolve source ----------
    int src_parent;
    std::string src_name;

    if (!resolvePath(srcpath, src_parent, src_name))
    {
        std::cout << "Source file not found\n";
        return -1;
    }

    dir_entry src_dir[MAX_DIR_ENTRIES];
    disk.read(src_parent, (uint8_t *)src_dir);

    int src_idx = findEntryIndex(src_dir, MAX_DIR_ENTRIES, src_name);
    if (src_idx == -1 || src_dir[src_idx].type == TYPE_DIR)
    {
        std::cout << "Source file not found\n";
        return -1;
    }

    dir_entry &src_entry = src_dir[src_idx];

    // ---------- 2) Resolve destination ----------
    int dst_parent;
    std::string dst_name;

    if (!resolvePath(dstpath, dst_parent, dst_name))
    {
        std::cout << "Destination not found\n";
        return -1;
    }

    dir_entry dst_dir[max];
    disk.read(dst_parent, (uint8_t *)dst_dir);

    // If destination name exists and is a directory -> copy into it using same src name
    int dst_idx = findEntryIndex(dst_dir, max, dst_name);
    if (dst_idx != -1 && dst_dir[dst_idx].type == TYPE_DIR)
    {
        dst_parent = dst_dir[dst_idx].first_blk;
        disk.read(dst_parent, (uint8_t *)dst_dir);
        dst_name = src_name;
    }

    // ---------- 3) Noclobber: destination file must not exist ----------
    if (findEntryIndex(dst_dir, MAX_DIR_ENTRIES, dst_name) != -1)
    {
        std::cout << "File already exists\n";
        return -1;
    }

    // ---------- 4) Find free entry slot ----------
    int free_idx = findFreeIndex(dst_dir, max);
    if (free_idx == -1)
    {
        std::cout << "Directory full\n";
        return -1;
    }

    // ---------- 5) Allocate blocks ----------
    disk.read(FAT_BLOCK, (uint8_t *)fat);

    int size = src_entry.size;
    int blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    std::vector<int> blocks;
    for (int i = 2; i < disk.get_no_blocks() && (int)blocks.size() < blocks_needed; i++)
    {
        if (fat[i] == FAT_FREE)
            blocks.push_back(i);
    }

    if ((int)blocks.size() < blocks_needed)
    {
        std::cout << "Not enough disk space\n";
        return -1;
    }

    // ---------- 6) Copy data block-by-block and link FAT ----------
    int cur = src_entry.first_blk;

    for (int i = 0; i < (int)blocks.size(); i++)
    {
        uint8_t buf[BLOCK_SIZE] = {0};
        disk.read(cur, buf);
        disk.write(blocks[i], buf);

        fat[blocks[i]] = (i + 1 < (int)blocks.size()) ? blocks[i + 1] : FAT_EOF;
        cur = fat[cur]; // follow source chain
    }

    // ---------- 7) Create destination directory entry ----------
    std::strncpy(dst_dir[free_idx].file_name, dst_name.c_str(), MAX_NAME_LEN);
    dst_dir[free_idx].file_name[MAX_NAME_LEN] = '\0';
    dst_dir[free_idx].type = TYPE_FILE;
    dst_dir[free_idx].size = size;
    dst_dir[free_idx].first_blk = blocks[0];

    // ---------- 8) Persist ----------
    disk.write(dst_parent, (uint8_t *)dst_dir);
    disk.write(FAT_BLOCK, (uint8_t *)fat);

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string srcpath, std::string dstpath)
{
    // ---------- 1) Resolve source ----------
    int src_parent;
    std::string src_name;

    if (!resolvePath(srcpath, src_parent, src_name))
    {
        std::cout << "File not found\n";
        return -1;
    }

    dir_entry src_dir[MAX_DIR_ENTRIES];
    disk.read(src_parent, (uint8_t *)src_dir);

    int src_idx = findEntryIndex(src_dir, MAX_DIR_ENTRIES, src_name);
    if (src_idx == -1)
    {
        std::cout << "File not found\n";
        return -1;
    }

    if (isDir(src_dir[src_idx]))
    {
        std::cout << "Cannot move directory\n";
        return -1;
    }

    // ---------- 2) Resolve destination ----------
    int dst_parent;
    std::string dst_name;

    if (!resolvePath(dstpath, dst_parent, dst_name))
    {
        std::cout << "Destination not found\n";
        return -1;
    }

    dir_entry dst_dir[MAX_DIR_ENTRIES];
    disk.read(dst_parent, (uint8_t *)dst_dir);

    // If dst_name exists and is a directory => move into it, keep same filename
    int dst_idx = findEntryIndex(dst_dir, MAX_DIR_ENTRIES, dst_name);
    if (dst_idx != -1 && dst_dir[dst_idx].type == TYPE_DIR)
    {
        dst_parent = dst_dir[dst_idx].first_blk;
        disk.read(dst_parent, (uint8_t *)dst_dir);
        dst_name = src_name;
    }

    // ---------- 3) Noclobber: destination name must not exist ----------
    if (findEntryIndex(dst_dir, MAX_DIR_ENTRIES, dst_name) != -1)
    {
        std::cout << "File already exists\n";
        return -1;
    }

    // ---------- 4) Find free slot in destination directory ----------
    int free_idx = findFreeIndex(dst_dir, MAX_DIR_ENTRIES);
    if (free_idx == -1)
    {
        std::cout << "Directory full\n";
        return -1;
    }

    // ---------- 5) Move directory entry ----------
    dst_dir[free_idx] = src_dir[src_idx];

    std::strncpy(dst_dir[free_idx].file_name, dst_name.c_str(), MAX_NAME_LEN);
    dst_dir[free_idx].file_name[MAX_NAME_LEN] = '\0';

    std::memset(&src_dir[src_idx], 0, sizeof(dir_entry));

    // ---------- 6) Write back ----------
    disk.write(src_parent, (uint8_t *)src_dir);
    disk.write(dst_parent, (uint8_t *)dst_dir);

    return 0;
}

int FS::rm(std::string path)
{
    int parentBlk;
    std::string name;

    // 1) Resolve path -> parent directory block + object name
    if (!resolvePath(path, parentBlk, name))
    {
        std::cout << "File not found\n";
        return -1;
    }

    // 2) Read parent directory (where the entry lives)
    dir_entry parentDir[MAX_DIR_ENTRIES];
    disk.read(parentBlk, (uint8_t *)parentDir);

    // 3) Find the entry to remove
    int idx = findEntryIndex(parentDir, MAX_DIR_ENTRIES, name);
    if (idx == -1)
    {
        std::cout << "File not found\n";
        return -1;
    }

    dir_entry &entry = parentDir[idx];

    // 4) Check WRITE permission on the object itself
    if (!(entry.access_rights & WRITE))
    {
        std::cout << "Permission denied\n";
        return -1;
    }

    // 5) If entry is a directory, ensure it is empty (only ".." allowed)
    if (entry.type == TYPE_DIR)
    {
        dir_entry subDir[MAX_DIR_ENTRIES];
        disk.read(entry.first_blk, (uint8_t *)subDir);

        for (int i = 0; i < MAX_DIR_ENTRIES; i++)
        {
            if (subDir[i].file_name[0] != '\0' &&
                std::strcmp(subDir[i].file_name, "..") != 0)
            {
                std::cout << "Directory not empty\n";
                return -1;
            }
        }
    }

    // 6) Free all FAT blocks used by the file/directory content
    disk.read(FAT_BLOCK, (uint8_t *)fat);

    int cur = entry.first_blk;
    while (cur != FAT_EOF)
    {
        int next = fat[cur];
        fat[cur] = FAT_FREE;
        cur = next;
    }

    // 7) Remove the directory entry (mark as empty)
    std::memset(&parentDir[idx], 0, sizeof(dir_entry));

    // 8) Write back changes
    disk.write(parentBlk, (uint8_t *)parentDir);
    disk.write(FAT_BLOCK, (uint8_t *)fat);

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
    int parent1, parent2;
    std::string name1, name2;

    // 1) Resolve both paths
    if (!resolvePath(filepath1, parent1, name1) ||
        !resolvePath(filepath2, parent2, name2))
    {
        std::cout << "File not found\n";
        return -1;
    }

    // 2) Read both parent directories
    dir_entry dir1[MAX_DIR_ENTRIES], dir2[MAX_DIR_ENTRIES];
    disk.read(parent1, (uint8_t *)dir1);
    disk.read(parent2, (uint8_t *)dir2);

    // 3) Find source and destination entries
    int srcIdx = findEntryIndex(dir1, MAX_DIR_ENTRIES, name1);
    int dstIdx = findEntryIndex(dir2, MAX_DIR_ENTRIES, name2);

    if (srcIdx == -1 || dstIdx == -1)
    {
        std::cout << "File not found\n";
        return -1;
    }

    dir_entry &src = dir1[srcIdx];
    dir_entry &dst = dir2[dstIdx];

    // Optional but clearer/safe: both must be files
    if (src.type == TYPE_DIR || dst.type == TYPE_DIR)
    {
        std::cout << "Not a file\n";
        return -1;
    }

    // 4) Permissions
    if (!(src.access_rights & READ) || !(dst.access_rights & WRITE))
    {
        std::cout << "Permission denied\n";
        return -1;
    }

    // 5) Load FAT
    disk.read(FAT_BLOCK, (uint8_t *)fat);

    // 6) Read all data from file1 into RAM
    int size1 = src.size;
    std::vector<uint8_t> data1(size1);

    int srcBlk = src.first_blk;
    int offset = 0;

    while (srcBlk != FAT_EOF && offset < size1)
    {
        uint8_t buf[BLOCK_SIZE];
        disk.read(srcBlk, buf);

        int n = std::min(BLOCK_SIZE, size1 - offset);
        std::memcpy(&data1[offset], buf, n);

        offset += n;
        srcBlk = fat[srcBlk];
    }

    // 7) Find last block of file2
    int lastBlk = dst.first_blk;
    while (fat[lastBlk] != FAT_EOF)
        lastBlk = fat[lastBlk];

    // 8) Append into the last block of file2 (fill remaining space)
    uint8_t last_buf[BLOCK_SIZE];
    disk.read(lastBlk, last_buf);

    int offset2 = dst.size % BLOCK_SIZE;
    int written = std::min(BLOCK_SIZE - offset2, size1);

    std::memcpy(last_buf + offset2, data1.data(), written);
    disk.write(lastBlk, last_buf);

    // 9) If more data remains, allocate new blocks and write
    int remaining = size1 - written;
    int pos = written;

    while (remaining > 0)
    {
        int new_blk = -1;
        for (int i = 2; i < disk.get_no_blocks(); i++)
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

        // Link new block to end of dst chain
        fat[lastBlk] = new_blk;
        fat[new_blk] = FAT_EOF;
        lastBlk = new_blk;

        // Write next chunk
        uint8_t buf[BLOCK_SIZE] = {0};
        int n = std::min(BLOCK_SIZE, remaining);
        std::memcpy(buf, data1.data() + pos, n);
        disk.write(new_blk, buf);

        pos += n;
        remaining -= n;
    }

    // 10) Update file2 size and write back FAT + dst directory
    dst.size += size1;

    disk.write(FAT_BLOCK, (uint8_t *)fat);
    disk.write(parent2, (uint8_t *)dir2);

    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
    int parentBlk;
    std::string name;

    // 1) Resolve path -> where to create + new directory name
    if (!resolvePath(dirpath, parentBlk, name))
    {
        std::cout << "Directory not found\n";
        return -1;
    }

    if (name.length() > MAX_NAME_LEN)
    {
        std::cout << "Directory name too long\n";
        return -1;
    }

    // 2) Read parent directory block
    dir_entry parentDir[MAX_DIR_ENTRIES];
    disk.read(parentBlk, (uint8_t*)parentDir);

    // 3) Name must not already exist
    int existing = findEntryIndex(parentDir, MAX_DIR_ENTRIES, name);
    if (existing != -1)
    {
        if (parentDir[existing].type == TYPE_DIR)
            std::cout << "Directory already exists\n";
        else
            std::cout << "File with same name exists\n";
        return -1;
    }

    // 4) Find free slot in parent directory
    int free_idx = findFreeIndex(parentDir, MAX_DIR_ENTRIES);
    if (free_idx == -1)
    {
        std::cout << "Directory full\n";
        return -1;
    }

    // 5) Read FAT and allocate a free block for the new directory
    disk.read(FAT_BLOCK, (uint8_t*)fat);

    int newDirBlk = -1;
    for (int i = 2; i < disk.get_no_blocks(); i++)
    {
        if (fat[i] == FAT_FREE)
        {
            newDirBlk = i;
            fat[i] = FAT_EOF;   // mark block as used (end of chain)
            break;
        }
    }

    if (newDirBlk == -1)
    {
        std::cout << "No free blocks\n";
        return -1;
    }

    // 6) Create the new directory block content:
    //    first entry ".." points to the parent directory block
    dir_entry newDir[MAX_DIR_ENTRIES];
    std::memset(newDir, 0, sizeof(newDir));

    std::strncpy(newDir[0].file_name, "..", MAX_NAME_LEN);
    newDir[0].file_name[MAX_NAME_LEN] = '\0';
    newDir[0].type = TYPE_DIR;
    newDir[0].first_blk = parentBlk;

    disk.write(newDirBlk, (uint8_t*)newDir);

    // 7) Add directory entry into the parent directory
    std::strncpy(parentDir[free_idx].file_name, name.c_str(), MAX_NAME_LEN);
    parentDir[free_idx].file_name[MAX_NAME_LEN] = '\0';
    parentDir[free_idx].type = TYPE_DIR;
    parentDir[free_idx].first_blk = newDirBlk;

    // (Optional) if your system expects directory permissions:
    // parentDir[free_idx].access_rights = READ | WRITE | EXECUTE;

    // 8) Write back parent directory and FAT
    disk.write(parentBlk, (uint8_t*)parentDir);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string path)
{
    int parentBlk;
    std::string name;

    // 1) Resolve path -> parent directory block + target name
    if (!resolvePath(path, parentBlk, name))
    {
        std::cout << "Directory not found\n";
        return -1;
    }

    // 2) Read parent directory
    dir_entry dir[MAX_DIR_ENTRIES];
    disk.read(parentBlk, (uint8_t*)dir);

    // 3) Find the entry
    int idx = findEntryIndex(dir, MAX_DIR_ENTRIES, name);
    if (idx == -1)
    {
        std::cout << "Directory not found\n";
        return -1;
    }

    dir_entry& entry = dir[idx];

    // 4) Must be a directory
    if (entry.type != TYPE_DIR)
    {
        std::cout << "Not a directory\n";
        return -1;
    }

    // 5) Need EXECUTE permission to enter
    if (!(entry.access_rights & EXECUTE))
    {
        std::cout << "Permission denied\n";
        return -1;
    }

    // 6) Change current working directory
    cwd_blk = entry.first_blk;
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
    if (cwd_blk == ROOT_BLOCK)
    {
        std::cout << "/\n";
        return 0;
    }

    std::vector<std::string> parts;
    int current = cwd_blk;

    while (current != ROOT_BLOCK)
    {
        dir_entry curDir[MAX_DIR_ENTRIES];
        disk.read(current, (uint8_t*)curDir);

        // 1) Find parent using ".."
        int parentIdx = findEntryIndex(curDir, MAX_DIR_ENTRIES, "..");
        if (parentIdx == -1)
            break;

        int parent = curDir[parentIdx].first_blk;

        // 2) Find the name of current directory inside parent directory
        dir_entry parentDir[MAX_DIR_ENTRIES];
        disk.read(parent, (uint8_t*)parentDir);

        for (int i = 0; i < MAX_DIR_ENTRIES; i++)
        {
            if (parentDir[i].type == TYPE_DIR &&
                parentDir[i].first_blk == current)
            {
                parts.push_back(parentDir[i].file_name);
                break;
            }
        }

        current = parent;
    }

    // print in reverse order
    std::cout << "/";
    for (int i = (int)parts.size() - 1; i >= 0; i--)
    {
        std::cout << parts[i];
        if (i > 0) std::cout << "/";
    }
    std::cout << "\n";

    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
    // 1) Convert accessrights to int
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

    // 2) Resolve path -> parent directory block + entry name
    int parentBlk;
    std::string name;

    if (!resolvePath(filepath, parentBlk, name))
    {
        std::cout << "File not found\n";
        return -1;
    }

    // 3) Read parent directory
    dir_entry dir[MAX_DIR_ENTRIES];
    disk.read(parentBlk, (uint8_t*)dir);

    // 4) Find entry and update rights
    int idx = findEntryIndex(dir, MAX_DIR_ENTRIES, name);
    if (idx == -1)
    {
        std::cout << "File not found\n";
        return -1;
    }

    dir[idx].access_rights = rights;
    disk.write(parentBlk, (uint8_t*)dir);
    return 0;
}
