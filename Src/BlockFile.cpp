/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011 Bart Trzynadlowski, Nik Henson
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free 
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/
 
/*
 * BlockFile.cpp
 *
 * Block format container file management. Implementation of the CBlockFile
 * class.
 */

#include "BlockFile.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include "Supermodel.h"

/******************************************************************************
 File or memory
******************************************************************************/

size_t CBlockFile::RawRead(void *data, size_t numBytes)
{
  if (!inMemory)
    return fp ? fread(data, 1, numBytes, fp) : 0;
  size_t n = memPos < mem.size() ? std::min(numBytes, mem.size() - memPos) : 0;
  if (n)
    memcpy(data, mem.data() + memPos, n);
  if (n < numBytes)
    memset(static_cast<uint8_t *>(data) + n, 0, numBytes - n);  // past the end reads as zeros
  memPos += n;
  return n;
}

void CBlockFile::RawWrite(const void *data, size_t numBytes)
{
  if (!inMemory)
  {
    if (fp)
      fwrite(data, 1, numBytes, fp);
    return;
  }
  if (memPos + numBytes > mem.size())
    mem.resize(memPos + numBytes);
  memcpy(mem.data() + memPos, data, numBytes);
  memPos += numBytes;
}

long int CBlockFile::Tell(void) const
{
  return inMemory ? (long int)memPos : (fp ? ftell(fp) : 0);
}

void CBlockFile::Seek(long int pos)
{
  if (inMemory)
    memPos = (size_t)pos;
  else if (fp)
    fseek(fp, pos, SEEK_SET);
}


/******************************************************************************
 Output Functions
******************************************************************************/

void CBlockFile::ReadString(std::string *str, uint32_t length)
{
  if (!IsOpen())
    return;
  str->clear();
  //TODO: use fstream to get rid of this ugly hack
  bool keep_loading = true;
  for (uint32_t i = 0; i < length; i++)
  {
    char c;
    RawRead(&c, 1);
    if (keep_loading)
    {
      if (!c)
        keep_loading = false;
      else
        *str += c;
    }
  }
}

unsigned CBlockFile::ReadBytes(void *data, uint32_t numBytes)
{
  if (!IsOpen())
    return 0;
  return (uint32_t)RawRead(data, numBytes);
}

unsigned CBlockFile::ReadDWord(uint32_t *data)
{
  if (!IsOpen())
    return 0;
  RawRead(data, sizeof(uint32_t));
  return 4;
}
  
void CBlockFile::UpdateBlockSize(void)
{
  long int  curPos;
  unsigned  newBlockSize;
  
  if (!IsOpen())
    return;
  curPos = Tell();       // save current file position
  Seek(blockStartPos);
  newBlockSize = curPos - blockStartPos;
  RawWrite(&newBlockSize, sizeof(uint32_t));
  Seek(curPos);  // go back
}

void CBlockFile::WriteByte(uint8_t data)
{
  if (!IsOpen())
    return;
  RawWrite(&data, sizeof(uint8_t));
  UpdateBlockSize();
}

void CBlockFile::WriteDWord(uint32_t data)
{
  if (!IsOpen())
    return;
  RawWrite(&data, sizeof(uint32_t));
  UpdateBlockSize();
}

void CBlockFile::WriteBytes(const void *data, uint32_t numBytes)
{
  if (!IsOpen())
    return;
  RawWrite(data, numBytes);
  UpdateBlockSize();
}

void CBlockFile::WriteBlockHeader(const std::string &name, const std::string &comment)
{
  if (!IsOpen())
    return;
  
  // Record current block starting position
  blockStartPos = Tell();

  // Write the total block length field
  WriteDWord(0);  // will be automatically updated as we write the file
  
  // Write name and comment lengths
  WriteDWord((uint32_t)name.size() + 1);
  WriteDWord((uint32_t)comment.size() + 1);
  Write(name);
  Write(comment);
  
  // Record the start of the current data section
  dataStartPos = Tell();
} 


/******************************************************************************
 Block Format Container File Implementation
 
 Files are just a consecutive array of blocks that must be searched.
 
 Block Format
 ------------
 blockLength  (uint32_t)  Total length of block in bytes.
 nameLength   (uint32_t)  Length of name field including terminating 0 (up to
              1025).
 commentLength  (uint32_t)  Same as above, but for comment string.
 name     ...     Name string (null-terminated, up to 1025 bytes).
 comment    ...     Comment string (same as above).
 data     ...     Raw data (blockLength - total header size).
******************************************************************************/

unsigned CBlockFile::Read(void *data, uint32_t numBytes)
{
  if (mode == 'r')
    return ReadBytes(data, numBytes);
  return 0;
}

unsigned CBlockFile::Read(bool *value)
{
  uint8_t byte;
  unsigned numBytes = Read(&byte, sizeof(byte));
  if (numBytes >  0)
    *value = byte != 0;
  return numBytes;
}

unsigned CBlockFile::Read(bool& value)
{
    return Read(&value);        // turn reference into pointer for above function
}

void CBlockFile::Write(const void *data, uint32_t numBytes)
{
  if (mode == 'w')
    WriteBytes(data, numBytes);
}

void CBlockFile::Write(bool value)
{
  uint8_t byte = value ? 1 : 0;
  Write(&byte, sizeof(byte));
}

void CBlockFile::Write(const std::string &str)
{
  if (mode == 'w')
      WriteBytes(str.c_str(), (uint32_t)str.length() + 1);
}

void CBlockFile::NewBlock(const std::string &name, const std::string &comment)
{
  if (mode == 'w')
    WriteBlockHeader(name, comment);
}

Result CBlockFile::FindBlock(const std::string &name)
{
  if (mode != 'r')
    return Result::FAIL;
    
  Seek(0);
  
  long int  curPos = 0;
  while (curPos < fileSize)
  {
    blockStartPos = curPos;
    
    // Read header
    uint32_t block_length;
    uint32_t name_length;
    uint32_t comment_length;
    curPos += ReadDWord(&block_length);
    curPos += ReadDWord(&name_length);
    curPos += ReadDWord(&comment_length);
    std::string block_name;
    ReadString(&block_name, name_length);
    
    // Is this the block we want?
    if (block_name == name)
    {
      Seek(blockStartPos + 12 + name_length + comment_length); // move to beginning of data
      dataStartPos = Tell();
      return Result::OKAY;
    }
    
    // Move to next block
    Seek(blockStartPos + block_length);
    curPos = blockStartPos + block_length;
    if (block_length == 0)  // this would never advance
      break;
  }
  
  return Result::FAIL;
}

Result CBlockFile::Create(const std::string &file, const std::string &headerName, const std::string &comment)
{
  fp = fopen(file.c_str(), "wb");
  if (!IsOpen())
    return Result::FAIL;
  mode = 'w';
  WriteBlockHeader(headerName, comment);
  return Result::OKAY;
}
  
Result CBlockFile::Load(const std::string &file)
{
  fp = fopen(file.c_str(), "rb");
  if (!IsOpen())
    return Result::FAIL;
  mode = 'r';
  
  // TODO: is this a valid block file?
  
  // Get the file size
  fseek(fp, 0, SEEK_END);
  fileSize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  
  return Result::OKAY;
}
  
Result CBlockFile::CreateInMemory(const std::string &headerName, const std::string &comment)
{
  Close();
  inMemory = true;
  mem.clear();
  memPos = 0;
  mode = 'w';
  WriteBlockHeader(headerName, comment);
  return Result::OKAY;
}

Result CBlockFile::LoadFromMemory(const void *data, size_t size)
{
  Close();
  inMemory = true;
  const uint8_t *p = static_cast<const uint8_t *>(data);
  mem.assign(p, p + size);
  memPos = 0;
  fileSize = (long int)size;
  mode = 'r';
  return Result::OKAY;
}

void CBlockFile::Close(void)
{
  if (fp != nullptr)
    fclose(fp);
  fp = nullptr;
  inMemory = false;
  mode = 0;
}

CBlockFile::CBlockFile(void) :
    fp(nullptr),
    mode(0),
    fileSize(0),
    blockStartPos(0),
    dataStartPos(0)
{
}

CBlockFile::~CBlockFile(void)
{
  if (fp != nullptr) // in case user forgot
    fclose(fp);
  fp = nullptr;
  mode = 0;
}
