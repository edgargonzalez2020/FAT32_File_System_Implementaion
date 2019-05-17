/*
 Name: Edgar Gonzalez
 ID: 1001336686
 */

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
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#define WHITESPACE " \t\n"
// We want to split our command line up into tokens
// so we need to define what delimits our tokens.
// In this case  white space
// will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size

#define MAX_NUM_ARGUMENTS 5     // Mav shell only supports five arguments

#define MAX_NUM_FILES     16

#define false 0

#define true  1

#define handle_error(msg) \
do {perror(msg); exit(EXIT_FAILURE);}while(0)

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
struct __attribute__((__packed__)) DirectoryEntryNull
{
  char      DIR_Name[12];
  uint8_t   DIR_Attr;
  uint8_t   Unused1[8];
  uint16_t  DIR_FirstClusterHigh;
  uint8_t   Unused2[4];
  uint16_t  DIR_FirstClusterLow;
  uint32_t  DIR_FileSize;
};
struct __attribute__((__packed__)) meta_info
{
  unsigned char BS_jmpBoot[3];
  uint64_t      BS_OEMName;
  uint16_t      BPB_BytsPerSec;
  uint8_t       BPB_SecPerClus;
  uint16_t      BPB_RsvdSecCnt;
  uint8_t       BPB_NumFATs;
  uint16_t      BPB_RootEntCnt;
  uint16_t      BPB_TotSec16;
  uint8_t       BPB_Media;
  uint16_t      BPB_FATSz16;
  uint16_t      BPB_SecPerTrk;
  uint16_t      BPB_NumHeads;
  uint32_t      BPB_HiddSec;
  uint32_t      BPB_TotSec32;
  uint64_t      BPB_FATSz32;
  uint16_t      BPB_ExtFlags;
  uint16_t      BPB_FSVer;
  uint64_t      BPB_RootClus;
  uint16_t      BPB_FSInfo;
  uint16_t      BPB_BkBootSec;
  unsigned char BPB_Reserved[12];
  uint8_t       BS_DrvNum;
  uint8_t       BS_Reserved1;
  uint8_t       BS_BootSig;
  uint64_t      BS_VolID;
  unsigned char BS_VolLab[11];
  uint64_t      BS_FilSysType;
};
struct meta_info inf; // Easily copy over fat32 meta data
const char * mem_block;

/*#####################################
 *          Functions
 *#####################################
 */
void    handle_sig_act( int );
void    read_fat32_image( char * );
char**  parse_input( char *, char * );
void    handle_open( char * );
void    handle_close( void );
void    handle_info( void );
void    handle_stat( char * );
void    handle_get( char * );
void    handle_put( char * );
void    handle_cd( struct DirectoryEntry *,char * ,int );
void    handle_ls( struct DirectoryEntry * ,char * );
void    handle_read( char *, char *, char * );
void    handle_cd_wrapper( struct DirectoryEntry *, char ** );
int16_t NextLB( uint32_t );
int     LBAToOffset( int32_t );
int     get_command_number( char * );
void    file_system_init( int );
int     find_file( struct DirectoryEntry * , char * , int * );
int     compare( char *, char *, char ** );
void    update_fat( int, char * );
int     find_unused_file( struct DirectoryEntry * );
int     count_back_paths( char * );

/*#####################################
 *          GLOBALS
 *#####################################
 */
struct  DirectoryEntry * ls_directory ;
struct  DirectoryEntry * current_working_directory;
uint16_t BPB_BytsPerSec;
uint8_t  BPB_SecPerClus;
uint16_t BPB_RsvdSecCnt;
uint8_t  BPB_NumFATs;
uint32_t BPB_FATSz32;

FILE*   fat32_image;
FILE*   write_file;
FILE*   put_file;
int32_t free_blocks_list[129152];

int     file_open       = false;
int     args_supplied   = true;
char*   fat_path;
int     number_of_files  = 16;
int     first_free_block = 0;
int     current_cluster  = 2;



int main( int argc, char ** argv )
{
//  struct sigaction act;
//  memset(&act, '\0', sizeof(struct sigaction));
//  act.sa_handler = &handle_sig_act;
//  if( sigaction(SIGINT, &act, NULL) < 0 )
//  {
//    perror("sigaction");
//    return 1;
//  }
  char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );
  while(1)
  {
    if( args_supplied )
    {
      handle_open(argv[1]);
      args_supplied = false;
    }
    printf ("mfs> ");
    char * working_root = NULL;
    char ** token = parse_input(working_root, cmd_str);
    int command_number = -1;
    command_number = get_command_number( token[0] );
    
    char * temp = (char *) malloc((strlen(token[1])+1)*sizeof(char));
    strcpy(temp, token[1]);
    
    switch ( command_number )
    {
      case 0:
        handle_open(temp);
        break;
      case 1:
        handle_close();
        break;
      case 2:
        handle_info();
        break;
      case 3:
        handle_stat(temp);
        break;
      case 4:
        handle_get(temp);
        break;
      case 5:
        handle_put(temp);
        break;
      case 6:
        handle_cd_wrapper(current_working_directory, &temp);
        break;
      case 7:
        handle_ls(current_working_directory,temp);
        break;
      case 8:
        handle_read(temp, token[2], token[3]);
        break;
      case 9:
      case 10:
        free(temp);
        exit( 0 );
    }
    free(temp);
  }
  
}

/*========================================================
 *             Returns an array of tokens.
 * args: char *, char *; save working_root to be freed
 * rvalue: char**; array of tokens
 * description: Returns a malloced array of strings
 * caller must free.
 *========================================================
 */
char ** parse_input( char * working_root, char * cmd_str )
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
    strncpy(token[token_count], arg_ptr, MAX_COMMAND_SIZE);
    if( strlen( token[token_count] ) == 0 )
    {
      break;
    }
    token_count++;
  }
  free( working_root );
  return token;
}
/*========================================================
 *                   Helper Function
 * args: struct DirectoryEntry *
 * rvalue: int
 * description: Searches the argument directory for a file
 * that is not currently in use, and returns it's index
 * returns -1 on failure.
 *========================================================
 */
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
/*========================================================
 *                Calculates next address
 * args: uint32_t
 * rvalue: int16_t
 * description: Calculates FAT offset and returns value
 * stored at offset
 *
 *========================================================
 */
int16_t NextLB( uint32_t sector )
{
  uint32_t FATAddress = (BPB_BytsPerSec * BPB_RsvdSecCnt) + (sector*4);
  int32_t val;
  fseek(fat32_image, FATAddress, SEEK_SET);
  fread(&val, 2, 1, fat32_image);
  return val;
}
/*========================================================
 *              Calculates cluster poffset
 * args: int32_t
 * rvalue: int
 * description: Calculates a cluster offset and returns it.
 *
 *========================================================
 */
int LBAToOffset( int32_t sector )
{
  return (( sector - 2 ) * BPB_BytsPerSec)
  + (BPB_BytsPerSec * BPB_RsvdSecCnt)
  + (BPB_NumFATs * BPB_FATSz32 * BPB_BytsPerSec);
}
/*========================================================
 *                 Writes data to FAT
 * args: int, char *
 * rvalue: void
 * description: This function calculates which blocks are
 * free and reserves them to be written for a file.
 * Copies sectors to both FAT1 and FAT2.
 * After the sectors have been written, it then writes
 * the file data to the appropriate cluster.
 *========================================================
 */
void update_fat( int file_size, char * buffer )
{
  int start_sector = free_blocks_list[first_free_block];
  free_blocks_list[first_free_block++] = 0;
  int sectors = file_size / BPB_BytsPerSec + 1;
  int * indxs = (int*)malloc((sectors + 1) * sizeof(int));
  int i;
  // reserve sector blocks to use
  for( i = 0; i < sectors; i++ )
  {
    indxs[i] = free_blocks_list[first_free_block];
    free_blocks_list[first_free_block++] = 0;
  }
  indxs[sectors] = -1; // mark the end of the sector chain

  // begin writing data to the sectors
  int current_block = start_sector;
  for( i = 0; i < sectors + 1; i++ )
  {
    int write_value = indxs[i];
    // calculate the FAT table offsets
    int FAT1_offset = (BPB_BytsPerSec * BPB_RsvdSecCnt) + (current_block * 4);
    int FAT2_offset = (BPB_BytsPerSec * BPB_RsvdSecCnt) +
    (BPB_FATSz32 * BPB_BytsPerSec) + (current_block*4);
    fseek(fat32_image, FAT1_offset, SEEK_SET);
    fwrite(&write_value, 4, 1, fat32_image);
    fseek(fat32_image, FAT2_offset, SEEK_SET);
    fwrite(&write_value, 4, 1, fat32_image);
    current_block = indxs[i];
  }
  // Write data to the clusters
  int j = 0;
  int next_cluster = start_sector;
  int curr_size = file_size;
  int idx = 0;
  do
  {
    int offset = LBAToOffset(next_cluster);
    fseek(fat32_image, offset, SEEK_SET);
    if( curr_size < 512 )
    {
      fwrite(&buffer[idx], curr_size, 1, fat32_image);
      break;
    }
    else if( curr_size >= 512 )
    {
      fwrite(&buffer[idx], BPB_BytsPerSec, 1, fat32_image);
      curr_size -= BPB_BytsPerSec;
      idx += (curr_size-1);
    }
    next_cluster = indxs[j];
    j++;
  }
  while(1);
  free(indxs);
}
/*========================================================
 *                    Helper Function
 * args: char *
 * rvalue: int
 * description: Returns the number of '/' contained
 * in path_name. The original pointer is modified to
 * return that last non ".." path.
 * e.g "../../test.txt" -> path_name="test.txt" returns 2.
 *
 *========================================================
 */
int count_back_paths( char * path_name )
{
  int count = 1;
  char * temp = (char*)malloc( strlen(path_name) + sizeof(char));
  temp[strlen(path_name)] = '\0';
  strncpy(temp, path_name, strlen(path_name));
  char * token = strtok( temp, "/");
  if( strcmp(token, "..") != 0 )
  {
    free(temp);
    return 0;
  }
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
/*========================================================
 *               Function provided by Prof
 *========================================================
 */
int compare( char * string1 , char * string2, char ** return_string )
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
  if( return_string != NULL )
  {
    strcpy(*return_string, expanded_name);
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
/*========================================================
 *                Finds file in directory
 * args: struct DirectoryEntry*, char *, int *
 * rvalue: int
 * description: Searches the arg directory for the file
 * containing name. If the file is found, the index is
 * returned by reference, -1 on failure.
 * returns a boolean if found.
 *
 *========================================================
 */
int find_file( struct DirectoryEntry * directory, char * name, int * index )
{
  int i;
  for( i = 0; i < number_of_files; i++ )
  {
    char * temp = (char*)malloc((strlen(name) + 1) * sizeof(char));
    temp[strlen(name)] = '\0';
    strncpy(temp, name, strlen(name));
    char dir_name[12];
    strncpy(dir_name, directory[i].DIR_Name, 11);
    dir_name[11] = '\0';
    if( compare(dir_name, temp, NULL) )
    {
      free(temp);
      *index = i;
      return true;
    }
    free(temp);
  }
  *index = -1;
  return false;
}
/*========================================================
 *                 Returns a command number
 * args: char *
 * rvalue: int
 * description: Based on the input string it returns a
 * predefined number to signify which command to run.
 *
 *========================================================
 */
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
/*========================================================
 *                   Initialize Values
 * args: int
 * rvalue: void
 * description: Initializes file system meta data and
 * populates the free blocks list.
 * It has an ancillary purpose to sync the cwd, after put.
 * The flag ensures we dont mess with the free list when
 * we only want to sync the cwd.
 *========================================================
 */
void file_system_init( int flag )
{
  read_fat32_image(fat_path);
  memcpy(&inf, mem_block, 90);
  BPB_BytsPerSec = inf.BPB_BytsPerSec;
  BPB_SecPerClus = inf.BPB_SecPerClus;
  BPB_RsvdSecCnt = inf.BPB_RsvdSecCnt;
  BPB_NumFATs = inf.BPB_NumFATs;
  BPB_FATSz32 = inf.BPB_FATSz32;
  
  //load up root dir
  int i = 0;
  int32_t  next_cluster = 2;
  current_working_directory = (struct DirectoryEntry *) malloc(16 * sizeof( *current_working_directory));
  struct DirectoryEntry * realloc_dir;
  do
  {
    if( i != 0 )
    {
      realloc_dir = (struct DirectoryEntry * )realloc( current_working_directory , sizeof(struct DirectoryEntry) * number_of_files *16);
      if( realloc_dir )
      {
        current_working_directory = realloc_dir;
      }
    }
    int offset = LBAToOffset(next_cluster);
    memcpy( &current_working_directory[i], (mem_block + offset),sizeof(struct DirectoryEntry)*16);
    next_cluster = NextLB(next_cluster);
    i += 15;
    number_of_files +=16;
  }
  while( next_cluster != -1 );
  
  if( !flag )
  {
    int i = 2;
    int k = 0;
    while( i < 129152 )
    {
      if( NextLB(i) == 0 )
      {
        free_blocks_list[k] = i;
        k++;
      }
      i++;
    }
  }
  
}
/*========================================================
 *                 Returns string array
 * args: char *, int
 * rvalue: char **
 * description: It tokenizes the string passed in, and
 * returns all the strings enclosed by "/", and their count.
 * e.g "foldera/folderc" -> ["foldera", "folderc"], 2
 *========================================================
 */
char ** get_cd_paths( char * path_name, int * number )
{
  char * temp = (char*)malloc((strlen(path_name) + 1) * sizeof(char));
  temp[strlen(path_name)] = '\0';
  strncpy(temp, path_name, strlen(path_name));
  char ** paths = (char**)malloc(255 * sizeof(char*));
  int i = 0;
  char * token = strtok(temp, "/");
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
  free(temp);
  return paths;
}
/*========================================================
 *                   Wrapper Function
 * args: char *
 * rvalue: void
 * description: Opens the file system
 *
 *========================================================
 */
void handle_open( char * path )
{
  if( file_open )
  {
    printf("Error: File system image already open.\n");
    return;
  }
  else
  {
    fat32_image = fopen(path, "r+");
  }
  
  if( fat32_image )
  {
    // Initialize file systrem variables, and load up root directory on start
    fat_path = path;
    file_system_init(false);
    file_open = true;
  }
  else
  {
    printf("Error: File system image not found.\n");
  }
}
/*========================================================
 *                   Wrapper function
 * args: void
 * rvalue: void
 * description: closes the file system.
 *
 *========================================================
 */
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
/*========================================================
 *                    Wrapper function
 * args: void
 * rvalue: void
 * description: Prints filesystem meta data
 *
 *========================================================
 */
void handle_info( void )
{
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
  printf("     BPB_BytsPerSec BPB_SecPerClus BPB_RsvdSecCnt BPB_NumFATs BPB_FATSz32\n");
  printf("Hex: %14X %14X %14X %11X %11X\n",BPB_BytsPerSec,BPB_SecPerClus,BPB_RsvdSecCnt,BPB_NumFATs,BPB_FATSz32);
  printf("Dec: %14d %14d %14d %11d %11d\n",BPB_BytsPerSec,BPB_SecPerClus,BPB_RsvdSecCnt,BPB_NumFATs,BPB_FATSz32);
}
/*========================================================
 *                  Wrapper function
 * args: char *
 * rvalue: void
 * description: prints file metadata pointed by name.
 *
 *========================================================
 */
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
  int index;
  int found = false;
  if( find_file(current_working_directory, name, &index) )
  {
    file_attr = current_working_directory[index].DIR_Attr;
    file_size = current_working_directory[index].DIR_FileSize;
    file_cluster = current_working_directory[index].DIR_FirstClusterLow;
    printf("%-10s %10s %10s\n","Attribute", "Size", "Cluster");
    printf("%-10X %10d %10d\n", file_attr, file_size, file_cluster);
    found = true;
  }
  if( !found )
  {
    printf("Error: File not found.\n");
  }
}
/*========================================================
 *                  Wrapper function
 * args: char *
 * rvalue: void
 * description: Grabs a file from the filesystem image
 * and brings it to the current working directory.
 *
 *========================================================
 */
void handle_get( char * name )
{
  char * temp = (char*)malloc((strlen(name) +1 )* sizeof(char));
  temp[strlen(name)] = '\0';
  strncpy(temp, name, strlen(name));
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
  int index;
  if( find_file(current_working_directory, temp, &index) )
  {
    uint32_t file_size = current_working_directory[index].DIR_FileSize;
    uint32_t original_file_size = file_size;
    int16_t current_cluster = current_working_directory[index].DIR_FirstClusterLow;
    write_file = fopen(name,"wb");
    int offset = LBAToOffset(current_cluster);
    int16_t next_cluster;
    uint32_t i = 0;
    char * buffer =(char*)malloc(8);
    do
    {
      memcpy(&buffer[i], (mem_block + offset), file_size);
      next_cluster = NextLB(current_cluster);
      current_cluster = next_cluster;
      offset = LBAToOffset(current_cluster);
      file_size -= BPB_BytsPerSec;
      i+= BPB_BytsPerSec;
    }
    while( next_cluster != -1 );
    if( write_file )
    {
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
/*========================================================
 *                  Wrapper function
 * args: char *
 * rvalue: void
 * description: Grabs a file pointer by path, and copies
 * it to the filesystem image. This function handles all
 * of the FAT allocation and cluster writing.
 *
 *========================================================
 */
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
    struct DirectoryEntry * local_dir = current_working_directory;
    char * buffer;
    fseek(put_file, 0, SEEK_END);
    int file_size = (int)ftell(put_file);
    fseek(put_file, 0, SEEK_SET);
    buffer = (char*)malloc((file_size+1)*sizeof(char));
    fread(buffer, file_size,1,put_file);
    fclose(put_file);
    
    int file_index = find_unused_file(local_dir);
//    This code is supposed to find a directory with an unused entry. Prob not neccessary.
//    if( file_index == -1 )
//    {
//      int k = 0;
//      int local_size = number_of_files;
//      while( k < local_size && file_index == -1 )
//      {
//        if( local_dir[k].DIR_Attr == 0x10 )
//        {
//          local_dir = handle_cd(local_dir, local_dir[k].DIR_Name, false);
//          file_index = find_unused_file(local_dir);
//        }
//        k++;
//      }
//    }
    uint8_t temp1[4] = {0};
    uint8_t temp2[8] = {0};
    
    char * modified_string = (char*)malloc(12*sizeof(char));
    struct DirectoryEntry * file_to_replace = (struct DirectoryEntry *) malloc(sizeof(struct DirectoryEntry));
    // dirty trick just so I can get a modified string >:)
    compare("dummy", path, &modified_string);
    strncpy(file_to_replace[0].DIR_Name, modified_string, 11*sizeof(char));
    file_to_replace[0].DIR_Attr = 0x20;
    memcpy(file_to_replace[0].Unused1, temp2, 8 * sizeof(uint8_t));
    memcpy(file_to_replace[0].Unused2, temp1, 4 * sizeof(uint8_t));
    file_to_replace[0].DIR_FirstClusterHigh = 0;
    file_to_replace[0].DIR_FileSize = file_size;
    file_to_replace[0].DIR_FirstClusterLow = free_blocks_list[first_free_block];
    int first_data_block = first_free_block;
    free_blocks_list[first_free_block++] = 0;
    update_fat(file_size,buffer);
    int offset = LBAToOffset(current_cluster);
    offset += sizeof(struct DirectoryEntry)*file_index;
    memcpy(&file_to_replace[0], (mem_block+offset),sizeof(struct DirectoryEntry));
    fclose(fat32_image);
    fat32_image = fopen(fat_path, "r+");
  }
}
/*========================================================
 *                  Helper Function
 * args: struct DirectoryEntry *, char *, int
 * rvalue: void
 * description: This function searches the directory passed
 * in and modifies either the global cwd, or a global ls
 * array. If flag = 0 it modifies cwd, else it modifies
 * ls direcory. This function works ancillary to print
 * "ls .."
 *
 *========================================================
 */
void handle_cd( struct DirectoryEntry * directory ,char * path, int ls_flag)
{
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
  }
  int directory_index;
  if ( find_file(directory, path, &directory_index) )
  {
    int i = 0;
    struct DirectoryEntry directory_to_navigate = directory[directory_index];
    int32_t next_cluster = directory_to_navigate.DIR_FirstClusterLow;
    current_working_directory = (struct DirectoryEntry *) realloc(current_working_directory, sizeof(struct DirectoryEntry) * 16);
    struct DirectoryEntry * realloc_dir;
    do
    {
      if( next_cluster == 0 )
      {
        next_cluster = 2;
      }
      if( i != 0 )
      {
        realloc_dir = (struct DirectoryEntry * )realloc( current_working_directory , sizeof(struct DirectoryEntry) * number_of_files *16);
        if( realloc_dir )
        {
          current_working_directory = realloc_dir;
        }
      }
      int offset = LBAToOffset(next_cluster);
      // In order for 'ls' to work, we 'cd' into the directory then just loop over the contents.
      if( ls_flag )
      {
        memcpy(&ls_directory[i], (mem_block + offset), 16*sizeof(struct DirectoryEntry));
      }
      else
      {
        memcpy( &current_working_directory[i], (mem_block + offset),sizeof(struct DirectoryEntry)*16);

      }
      next_cluster = NextLB(next_cluster);
      i += 15;
      number_of_files +=16;
    }
    while( next_cluster != -1 );
  }
  else
  {
    printf("cd: %s: No such file or directory\n", path);
  }
}
/*========================================================
 *                  Wrapper Function
 * args: struct DirectoryEntry *, char **
 * rvalue: void
 * description: This function oversees that cd can work
 * properly on absolute and relative paths.
 *
 *========================================================
 */
void handle_cd_wrapper( struct DirectoryEntry * directory, char ** path )
{
  // We are going to be moving the pointer head so we need to be able to free the memory
  // count_back_paths modifies the argument string so we need to keep track of that pointer as well
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
  
  int back_paths = count_back_paths(*path);
  if( back_paths > 1 )
  {
    int j;
    for( j = 0; j < back_paths; j++ )
    {
      handle_cd(current_working_directory, "..", 0);
    }
  }
  else
  {
    int number_of_paths,k;
    char ** paths = get_cd_paths(*path, &number_of_paths);
    for( k = 0; k < number_of_paths; k++ )
    {
      handle_cd(current_working_directory, paths[k], 0);
      free(paths[k]);
    }
    free(paths);
  }
}
/*========================================================
 *                    Wrapper Function
 * args: struct DirectoryEntry *, char *
 * rvalue: void
 * description: prints the contents of directory, making
 * sure to only print valid entries.
 *
 *========================================================
 */
void handle_ls(struct DirectoryEntry * directory,char * path )
{
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
  if( strcmp(path, "..") == 0 )
  {
    // Same idea for handle_cd_wrapper
    // We keep track of the pointer head so that we can free it
    handle_cd(current_working_directory, "..", 1);
    int i;
    for( i = 0; i < number_of_files; i++ )
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
/*========================================================
 *                  Wrapper Function
 * args:
 * rvalue: char *, char *, char *
 * description: Prints the raw bytes of the file pointed
 * at by name, with an offset specified by position.
 *
 *========================================================
 */
void handle_read( char * name , char * position, char * number_of_bytes )
{
  if( !file_open )
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }
    int index;
    if( find_file(current_working_directory, name, &index) )
    {
      int file_position = atoi(position);
      int bytes = atoi(number_of_bytes);
      int original_bytes = bytes;
      char * buffer = (char*)malloc(bytes*sizeof(char));
      int16_t cluster = current_working_directory[index].DIR_FirstClusterLow;
      int offset = LBAToOffset(cluster);
      fseek(fat32_image, offset + file_position, SEEK_SET);
      fread(&buffer[0], bytes, 1, fat32_image);
      int k,formatter;
      for( k = 0, formatter = 1; k < original_bytes; k++, formatter++ )
      {
        printf("%#4X ", buffer[k]);
        if( formatter % 10 == 0 ) printf("\n");
      }
      printf("\n");
      free(buffer);
    }
}
void read_fat32_image( char * path )
{
  int fd;
  struct stat sb;
  fd = open( path, O_RDONLY );
  fstat( fd, &sb );
  mem_block = mmap( NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0 );
  if( mem_block == MAP_FAILED )
  {
    handle_error("mmap");
  }
}

void handle_sig_act(int sig_num)
{
  printf("Interrupt triggered: %d\n", sig_num);
}
