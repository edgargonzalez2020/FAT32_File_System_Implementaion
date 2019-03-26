/*
 Name:Edgar Gonzalez
 ID: 1001336686
 */

// The MIT License (MIT)
//
// Copyright (c) 2016, 2017 Trevor Bakker
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <stdint.h>

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
// so we need to define what delimits our tokens.
// In this case  white space
// will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size

#define MAX_NUM_ARGUMENTS 5     // Mav shell only supports five arguments

#define MAX_NUM_FILES     16

#define false 0

#define true  1

struct __attribute__((__packed__)) DirectoryEntry
{
  char      DIR_Name[11];
  uint8_t   DIR_Attr;
  uint8_t   Unused1[8];
  uint16_t  DIR_FirstClusterHigh;
  uint8_t   Unused2[4];
  uint16_t  DIR_FirstClusterLow;
  uint32_t  DIR_FileSize;
};

struct DirectoryEntry * current_working_directory;
struct DirectoryEntry * ls_directory;

/*
        FAT32 Variables
 */
int16_t BPB_BytsPerSec;
int8_t  BPB_SecPerClus;
int16_t BPB_RsvdSecCnt;
int8_t  BPB_NumFATs;
int32_t BPB_FATSz32;


/*
        Program Global's
 */
int     file_open = false;
FILE *  fat32_image;
FILE *  write_file;
FILE *  put_file;
int     number_of_files = 0;
int     ls_files = 0;

int32_t free_blocks_list[129152];


/*
        Function Prototypes
 */

char ** parse_input( char *, char * );

void    free_all( char **, char ** );

void    free_directory( void );

void    file_system_init( void );

int     LBAToOffset( int32_t );

int     get_command_number( char * );

int compare( char *, char * );

int16_t NextLB( uint32_t );

int count_back_paths( char * );

char ** get_cd_paths( char *, int * );

int find_file( struct DirectoryEntry * , char * , int * );

void handle_open( char * );
void handle_close( void );
void handle_info( void );
void handle_stat( char * );
void handle_get( char * );
void handle_put( char * );
void handle_cd( char *, int );
void handle_ls( char * );
void handle_read( char *, char *, char * );

void test_function( char * token[] )
{
  int sector = atoi(token[1]);
  printf("Sector: %d\n", sector);
  printf("LBAToOffset: %d\n", LBAToOffset(sector));
  printf("LBAToOffset: %X\n", LBAToOffset(sector));
  printf("Next LBA: %d\n", NextLB(sector));
  printf("Next LBA: %X\n", NextLB(sector));
}

int find_unused_file( struct DirectoryEntry * );
int main()
{
  
  char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );
  
  while( 1 )
  {
    // Print out the mfs prompt
    printf ("mfs> ");
    char * working_root = NULL;
    char ** token = parse_input(working_root, cmd_str);
    
    if( token[0] != NULL )
    {
      if( strcmp(token[0], "test") == 0 )
      {
        if( !file_open ) printf("Error: File system image must be opened first.\n");
        test_function(token);
      }
      int command_number = get_command_number( token[0] );
      if( token[1] != NULL )
      {
        switch ( command_number )
        {
          case 0:
            handle_open(token[1]);
            break;
          case 3:
            handle_stat(token[1]);
            break;
          case 4:
            handle_get(token[1]);
            break;
          case 5:
            handle_put(token[1]);
            break;
          case 6:
            handle_cd(token[1],false);
            break;
          case 7:
            handle_ls(token[1]);
            break;
          case 8:
            handle_read(token[1], token[2], token[3]);
        }
      }
      else
      {
        switch ( command_number )
        {
          case 1:
            handle_close();
            break;
          case 2:
            handle_info();
            break;
          case 7:
            handle_ls("");
            break;
          case 9:
          case 10:
            free_all(token, &working_root);
            exit( 0 );
        }
      }
    }
    
    free_all(token, &working_root);
  }
  return 0;
}

char ** parse_input( char * working_root ,char * cmd_str )
{
  
  // Read the command from the commandline.  The
  // maximum command that will be read is MAX_COMMAND_SIZE
  // This while command will wait here until the user
  // inputs something since fgets returns NULL when there
  // is no input
  while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );
  
  /* Parse input */
  char ** token = (char**)malloc(MAX_NUM_ARGUMENTS * sizeof(char *));
  int i;
  for( i = 0; i < MAX_NUM_ARGUMENTS; i++ )
  {
    token[i] = (char*)malloc(MAX_COMMAND_SIZE * sizeof(char));
  }
  int   token_count = 0;
  
  // Pointer to point to the token
  // parsed by strsep
  char *arg_ptr;
  
  char *working_str  = strdup( cmd_str );
  
  // we are going to move the working_str pointer so
  // keep track of its original value so we can deallocate
  // the correct amount at the end
  working_root = working_str;
  
  // Tokenize the input stringswith whitespace used as the delimiter
  while ( ( (arg_ptr = strsep(&working_str, WHITESPACE ) ) != NULL) &&
         (token_count<MAX_NUM_ARGUMENTS))
  {
    token[token_count] = strndup( arg_ptr, MAX_COMMAND_SIZE );
    if( strlen( token[token_count] ) == 0 )
    {
      token[token_count] = NULL;
    }
    token_count++;
  }
  return token;
}

void free_all( char ** tokens, char ** working_root )
{
  int i;
  for( i = 0; i < MAX_NUM_ARGUMENTS; i++ )
  {
    if( tokens[i] != NULL )
      free(tokens[i]);
  }
  free(tokens);
  free(*working_root);
}


void free_directory()
{
  free(current_working_directory);
}
int LBAToOffset( int32_t sector )
{
  return (( sector - 2 ) * BPB_BytsPerSec) + (BPB_BytsPerSec * BPB_RsvdSecCnt) + (BPB_NumFATs * BPB_FATSz32 * BPB_BytsPerSec);
}


int16_t NextLB( uint32_t sector )
{
  uint32_t FATAddress = (BPB_BytsPerSec * BPB_RsvdSecCnt) + (sector*4);
  int32_t val;
  fseek(fat32_image, FATAddress, SEEK_SET);
  fread(&val, 2, 1, fat32_image);
  return val;
}
void file_system_init()
{
  // Get number of bytes per sector
  fseek(fat32_image, 11, SEEK_SET);
  fread(&BPB_BytsPerSec, 2, 1, fat32_image);
  //Get sectors per cluster
  fseek(fat32_image, 13, SEEK_SET);
  fread(&BPB_SecPerClus, 1, 1, fat32_image);
  //Get reserved sector count
  fseek(fat32_image, 14, SEEK_SET);
  fread(&BPB_RsvdSecCnt, 2, 1, fat32_image);
  //Get number of Fats
  fseek(fat32_image, 16, SEEK_SET);
  fread(&BPB_NumFATs, 1, 1, fat32_image);
  //Get fat size
  fseek(fat32_image, 36, SEEK_SET);
  fread(&BPB_FATSz32, 4, 1, fat32_image);
  
  //load up root dir
  int i = 0;
  int32_t  next_cluster = 2;
  current_working_directory = (struct DirectoryEntry *) malloc(16 * sizeof( *current_working_directory));
  do
  {
    if( i != 0 )
    {
      current_working_directory = (struct DirectoryEntry * )realloc( current_working_directory , sizeof(struct DirectoryEntry) * number_of_files *16);
    }
    int offset = LBAToOffset(next_cluster);
    fseek(fat32_image,offset, SEEK_SET);
    fread(&current_working_directory[i], sizeof(struct DirectoryEntry), 16, fat32_image);
    next_cluster = NextLB(next_cluster);
    i += 15;
    number_of_files +=16;
  }
  while( next_cluster != -1 );
}


int get_command_number( char * command )
{
  if( strcmp(command, "open") == 0 )       return 0;
  else if( strcmp(command, "close") == 0 ) return 1;
  else if( strcmp(command, "info") == 0 )  return 2;
  else if( strcmp(command, "stat") == 0 )  return 3;
  else if( strcmp(command, "get") == 0 )   return 4;
  else if( strcmp(command, "put") == 0 )   return 5;
  else if( strcmp(command, "cd") == 0 )    return 6;
  else if( strcmp(command, "ls") == 0 )    return 7;
  else if( strcmp(command, "read") == 0 )  return 8;
  else if( strcmp(command, "quit") == 0 )  return 9;
  else if( strcmp(command, "exit") == 0 )  return 10;
  else                                     return -1;
}


void handle_open( char * path )
{
  if( file_open )
  {
    printf("Error: File system image already open.\n");
    return;
  }
  else
  {
    fat32_image = fopen(path, "r");
  }
  
  if( fat32_image )
  {
    // Initialize file systrem variables, and load up root directory on start
    file_system_init();
    file_open = true;
  }
  else
  {
    printf("Error: File system image not found.\n");
  }
}
void handle_close( void )
{
  if( !file_open )
  {
    printf("Error: File system not open.\n");
  }
  else
  {
    fclose(fat32_image);
    file_open = false;
  }
}
void handle_info( void )
{
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
  printf("BPB_BytsPerSec: %d %X\n", BPB_BytsPerSec, BPB_BytsPerSec);
  printf("BPB_SecPerClus: %d %X\n", BPB_SecPerClus, BPB_SecPerClus);
  printf("BPB_RsvdSecCnt: %d %X\n", BPB_RsvdSecCnt, BPB_RsvdSecCnt);
  printf("BPB_NumFATs: %d %X\n", BPB_NumFATs, BPB_NumFATs);
  printf("BPB_FATSz32: %d %X\n", BPB_FATSz32, BPB_FATSz32);
}
void handle_stat( char * name )
{
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
  uint8_t   file_attr;
  uint32_t  file_size;
  uint16_t  file_cluster;
  int i;
  int found = false;
  for( i = 0; i < MAX_NUM_FILES; i++ )
  {
    char curr_working_string[12];
    curr_working_string[11] = '\0';
    memcpy(curr_working_string, current_working_directory[i].DIR_Name, 11);
    if( compare(curr_working_string, name) )
    {
      file_attr = current_working_directory[i].DIR_Attr;
      file_size = current_working_directory[i].DIR_FileSize;
      file_cluster = current_working_directory[i].DIR_FirstClusterLow;
      printf("%-10s %10s %10s\n","Attribute", "Size", "Cluster");
      printf("%-10d %10d %10d\n", file_attr, file_size, file_cluster);
      found = true;
    }
  }
  if( !found )
  {
    printf("Error: File not found.\n");
  }
}
void handle_get( char * name )
{
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
  int index;
  if( find_file(current_working_directory, name, &index) )
  {
    uint32_t file_size = current_working_directory[index].DIR_FileSize;
    uint32_t original_file_size = file_size;
    int16_t current_cluster = current_working_directory[index].DIR_FirstClusterLow;
    char * file_name = (char*)malloc(strlen(current_working_directory[index].DIR_Name)+3*sizeof(char));
    file_name[strlen(current_working_directory[index].DIR_Name)-2] = '\0';
    strcat(file_name, "./");
    strcat(file_name, name);
    write_file = fopen(file_name,"wb");
    int offset = LBAToOffset(current_cluster);
    int16_t next_cluster;
    uint32_t i = 0;
    char * buffer =(char*)malloc(8);
    do
    {
      fseek(fat32_image, offset, SEEK_SET);
      fread(&buffer[i],file_size,1, fat32_image);
      next_cluster = NextLB(current_cluster);
      current_cluster = next_cluster;
      offset = LBAToOffset(current_cluster);
      file_size -= BPB_BytsPerSec;
      i+= BPB_BytsPerSec;
    }
    while( next_cluster != -1 );
    if( write_file )
    {
      printf("Successfully wrote\n");
      fwrite(buffer, original_file_size, 1, write_file);
      fclose(write_file);
    }
  }
  else
  {
    printf("Error: File not found\n");
    return;
  }
  
}
void handle_put( char * path )
{
  
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
  put_file = fopen(path, "r");
  if( put_file )
  {
    // Update buffer based on file contents
    char * buffer;
    fseek(put_file, 0, SEEK_END);
    uint32_t file_size = (uint32_t)ftell(put_file);
    fseek(put_file, 0, SEEK_SET);
    buffer = (char*)malloc((file_size+1)*sizeof(char));
    fread(buffer, file_size,1,put_file);
    fclose(put_file);
    uint32_t FATAddress = BPB_BytsPerSec * BPB_RsvdSecCnt;
    int32_t i = 2;
    int k = 0; // index for free_blocks_list

    // Populate free blocks list
    while( i < 129152 )
    {
      if( NextLB(i) == 0 )
      {
        free_blocks_list[k] = i;
        k++;
      }
      i++;
    }

    i = 2; // reset i to root sector
    k = 0; // reset free block
    int file_index = find_unused_file(current_working_directory);
    
  }
}
void handle_cd( char * path, int ls_flag )
{
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
  
  int back_paths = count_back_paths(path);
  int iters = 0;
  int local_number = number_of_files;
  if( back_paths > 1 )
  {
    while( iters < back_paths )
    {
      int i;
      int old_number_of_files = local_number;
      for( i = 0; i < old_number_of_files; i++ )
      {
        if( compare(current_working_directory[i].DIR_Name, "..") )
        {
          int j = 0;
          int32_t  next_cluster = current_working_directory[i].DIR_FirstClusterLow;
          free_directory();
          current_working_directory = (struct DirectoryEntry *)malloc(sizeof(struct DirectoryEntry) * 16);
          local_number = 0;
          number_of_files = 0;
          do
          {
            if( j != 0 )
            {
              current_working_directory = (struct DirectoryEntry * )realloc( current_working_directory , sizeof(struct DirectoryEntry) * number_of_files *16);
            }
            if( next_cluster == 0 )
            {
              next_cluster = 2;
            }
            int offset = LBAToOffset(next_cluster);
            fseek(fat32_image,offset, SEEK_SET);
            fread(&current_working_directory[j], sizeof(struct DirectoryEntry), 16, fat32_image);
            next_cluster = NextLB(next_cluster);
            j += 15;
            local_number +=16;
            number_of_files += 16;
          }
          while( next_cluster != -1 );
        }
      }
      iters++;
    }
  }
  
  int number_of_paths;
  char ** paths = get_cd_paths(path, &number_of_paths);
  int i,k;
  for(k = 0; k < number_of_paths; k++ )
  {
    char * temp_copy = (char *)malloc( strlen(paths[k]) + sizeof(char) );
    temp_copy[ strlen(paths[k]) ] = '\0';
    strncpy(temp_copy, paths[k], strlen(paths[k]));
    for( i = 0; i < local_number; i++ )
    {
      if( compare(current_working_directory[i].DIR_Name, temp_copy) )
      {
        int j = 0;
        ls_files = 0;
        int32_t  next_cluster = current_working_directory[i].DIR_FirstClusterLow;
        if( next_cluster == 0 )
        {
          next_cluster = 2;
        }
        if( !ls_flag )
        {
          free_directory();
          current_working_directory = (struct DirectoryEntry *)malloc(sizeof(struct DirectoryEntry) * 16);
          number_of_files = 0;
        }
        else
        {
          ls_directory = (struct DirectoryEntry *)malloc(sizeof(struct DirectoryEntry) * 16);
        }
        do
        {
          if( j != 0 )
          {
            if( !ls_flag )
            {
              current_working_directory = (struct DirectoryEntry * )realloc( current_working_directory , sizeof(struct DirectoryEntry) * number_of_files *16);
            }
            else
            {
              ls_directory = (struct DirectoryEntry * )realloc( ls_directory , sizeof(struct DirectoryEntry) * (ls_files) * 16);
            }
          }
          int offset = LBAToOffset(next_cluster);
          fseek(fat32_image,offset, SEEK_SET);
          if( !ls_flag )
          {
            fread(&current_working_directory[j], sizeof(struct DirectoryEntry), 16, fat32_image);
            number_of_files +=16;
          }
          else
          {
            fread(&ls_directory[j], sizeof(struct DirectoryEntry), 16, fat32_image);
          }
          next_cluster = NextLB(next_cluster);
          j += 15;
          ls_files += 16;
        }
        while( next_cluster != -1 );
        local_number = number_of_files;
      }
    }
    free(temp_copy);
  }
  for( i = 0; i < number_of_paths; i++ )
  {
    free(paths[i]);
  }
  free(paths);
}
void handle_ls( char * path )
{
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
  if( strcmp(path, "..") == 0 )
  {
    handle_cd("..", true);
    int i;
    if( !ls_directory ) return;
    for( i = 0; i < ls_files; i++ )
    {
      uint8_t attr = ls_directory[i].DIR_Attr;
      char temp = ls_directory[i].DIR_Name[0];
      if( (attr == 0x01 || attr == 0x10 || attr == 0x20) && temp != '\xE5' && temp != '\x00' )
      {
        char curr_working_str[12];
        curr_working_str[11] = '\0';
        strncpy(curr_working_str, ls_directory[i].DIR_Name, 11);
        printf("%s\n", curr_working_str);
      }
    }
  }
  else
  {
    int i;
    for( i = 0; i < number_of_files; i++ )
    {
      uint8_t attr = current_working_directory[i].DIR_Attr;
      char temp = current_working_directory[i].DIR_Name[0];
      if( (attr == 0x01 || attr == 0x10 || attr == 0x20) && temp != '\xE5' && temp != '\x00' )
      {
        char curr_working_str[12];
        curr_working_str[11] = '\0';
        strncpy(curr_working_str, current_working_directory[i].DIR_Name, 11);
        printf("%s\n", curr_working_str);
      }
    }
  }
}
void handle_read( char * name , char * position, char * number_of_bytes )
{
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
}

int compare( char * string1 , char * string2 )
{
  char * lhs = (char*)malloc(strlen(string1) + sizeof(char));
  char * rhs = (char*)malloc(strlen(string2) + sizeof(char));
  strcpy(lhs, string1);
  strcpy(rhs, string2);
  char expanded_name[12];
  memset( expanded_name, ' ', 12 );
  
  if( lhs[0] == '.' && lhs[1] =='.' )
  {
    if( strcmp(rhs, "..") == 0 )
    {
      free(lhs);
      free(rhs);
      return true;
    }
  }
  else if( lhs[0] == '.' && lhs[1] != '.' )
  {
    free(lhs);
    free(rhs);
    return false;
  }
  
  char *token = strtok( rhs, "." );
  if( !token )
  {
    free(lhs);
    free(rhs);
    return false;
  }
  size_t token_size = strlen(token);
  strncpy( expanded_name, token, token_size );
  
  token = strtok( NULL, "." );
  
  if( token )
  {
    strncpy( (char*)(expanded_name+8), token, strlen(token ) );
  }
  
  expanded_name[11] = '\0';
  
  int i;
  for( i = 0; i < 11; i++ )
  {
    expanded_name[i] = toupper( expanded_name[i] );
  }
  
  if( strncmp( expanded_name, lhs, 11 ) == 0 )
  {
    free(lhs);
    free(rhs);
    return true;
  }
  free(lhs);
  free(rhs);
  return false;
}

int count_back_paths( char * path_name )
{
  int count = 1;
  char * temp = (char*)malloc( strlen(path_name) + sizeof(char));
  temp[strlen(path_name) - 1] = '\0';
  strncpy(temp, path_name, strlen(path_name));
  char * token = strtok( temp, "/");
  if( strcmp(token, "..") != 0 ) return 0;
  while( (token = strtok(NULL, "/"))  )
  {
    if( strcmp(token, "..") == 0 ) count++;
    else
    {
      //copy last non '..' path
      strncpy(path_name, token, strlen(path_name));
    }
  }
  free(temp);
  return count;
}

char ** get_cd_paths( char * path_name, int * number )
{
  char ** paths = (char**)malloc(255 * sizeof(char*));
  int i = 0;
  char * token = strtok(path_name, "/");
  if( token )
  {
    size_t token_size = strlen(token);
    paths[i] = (char*)malloc((token_size + 1 )*sizeof(char));
    strncpy(paths[0], token, token_size);
    paths[i++][token_size] = '\0';
  }
  *number = i;
  while( (token = strtok(NULL, "/")) != NULL )
  {
    size_t token_size = strlen(token);
    paths[i] = (char*)malloc((token_size + 1)*sizeof(char));
    strncpy(paths[i], token, token_size);
    paths[i][token_size] = '\0';
    i++;
  }
  *number = i;
  return paths;
}

int find_file( struct DirectoryEntry * directory, char * name, int * index )
{
  int i;
  char * temp = (char*)malloc(strlen(name)+ sizeof(char));
  for( i = 0; i < number_of_files; i++ )
  {
    temp[strlen(name)] = '\0';
    strncpy(temp, name, strlen(name));
    if( compare(directory[i].DIR_Name, temp) )
    {
      free(temp);
      *index = i;
      return true;
    }
  }
  free(temp);
  *index = -1;
  return false;
}

int find_unused_file( struct DirectoryEntry * directory )
{
  int i;
  for( i = 0; i < number_of_files; i++ )
  {
    if( directory[i].DIR_Name[0] == '\xE5' || directory[i].DIR_Name[0] == '\x00')
    {
      return i;
    }
  }
  return -1;
}


