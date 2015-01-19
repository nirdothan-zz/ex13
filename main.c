/*
 * main.c
 *
 *  Created on: Jan 12, 2015
 *      Author: Nir Dothan 028434009
 */

/*
 *                                   file system blocks
 *
 *		block 0				block 1				block 2			block 3				block 4	      block 5
 *  +-----------------+-----------------+-----------------+-----------------+-----------------+-----------------+
 *  |   Boot          |   Superblock    |  Group Desc     |  data bitmap	|  inode bitmap	  | inode table		|
 *  +-----------------+-----------------+-----------------+-----------------+-----------------+-----------------+
 *  																						  ^
 *  																						  |
 * 																			  inode_tab_block * block_size
 *
 *
 *
 *
 * 									inode table
 *
 * 	  inode 1	inode 2	  inode 3  inode 4   inode 5	inode 6	 inode 7	inode 8	  inode 9
 *  +---------+---------+---------+---------+---------+---------+---------+---------+---------+
 *  |		  |root dir |         |         |         |         |         |         |         |
 *  +---------+---------+---------+---------+---------+---------+---------+---------+---------+
 *
 *            ^
 *  		  |
 *  	inode_tab_block * block_size +
 *      (EXT2_ROOT_INO-1) * sizeof(inode)
 *
 *
 */
#include <stdio.h>
#include <linux/ext2_fs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/stat.h>
#include <string.h>
#include <time.h>
#define SUPERBLOCK_OFFSET 1024

/* used for inode indirection hierarchy */
enum indirection_level {
	LEAF, FIRST, SECOND, THIRD
};

enum operation_mode {SEARCH, PRINT};

static int fid, /* global variable set by the open() function */
block_size, /* bytes per sector from disk geometry */
num_block_groups, /* n= the number of block groups in the file system  */
inode_tab_block; /* the block number of the first element of the inode table */

/*
 * parseSuperblock
 *
 * 	populate basic global variables for file system
 *
 */
void parseSuperblock() {
	struct ext2_super_block sb;
	unsigned int baseblocksize = 1024;
	int pos = lseek(fid, SUPERBLOCK_OFFSET, SEEK_SET);
	if (pos != SUPERBLOCK_OFFSET) {
		printf("invalid EXT2 file: cannot seek superblock");
		exit(1);
	}
	if (read(fid, &sb, sizeof(sb)) != sizeof(sb)) {
		printf("invalid EXT2 file: cannot read full superblock");
		exit(1);
	}

	block_size = baseblocksize << sb.s_log_block_size;
	num_block_groups = (sb.s_blocks_count + sb.s_blocks_per_group - 1)
			/ sb.s_blocks_per_group;

}

/*
 * fd_read
 * 	input:
 * 		- block_number: the requested block number to read.
 * 		- buffer: allocated memory pointer for the read buffer
 * 	returns:
 * 		- the number of read bytes
 *
 * 	read a given block number from the file system into buffer
 */
int fd_read(int block_number, char *buffer) {
	int dest, len;
	dest = lseek(fid, block_number * block_size, SEEK_SET);
	if (dest != block_number * block_size) {
		printf("invalid EXT2 file: cannot seek block number [%d]\n",
				block_number);
		exit(1);
	}
	len = read(fid, buffer, block_size);
	if (len != block_size) {
		printf(
				"invalid EXT2 file: cannot read  block number [%d]. Read only %d bytes\n",
				block_number, len);
	}
	return len;
}
/*
 * parseGroupDescriptor
 * input:
 * 		- gd_num: the index of the group descriptor to parse. 0 is the first
 *
 * 	populate group level global variables for file system
 *
 */
void parseGroupDescriptor(int gd_num) {
	static struct ext2_group_desc gd;
	fd_read(2 + gd_num, &gd);
	inode_tab_block = gd.bg_inode_table;

}

/*
 * getInodeByInodeIndex
 * 		get an inode from the the file system
 * input:
 * 		- index: the index of the requested inode
 * returns:
 * 		- a pointer to the requested inode. Caller must free memory.
 *
 */
struct ext2_inode *getInodeByInodeIndex(int index) {
	struct ext2_inode *inode;
	/* the offset where the inode table begins */
	int offset = inode_tab_block * block_size;
	/* add the offset of previous inode records.  */
	offset += (index - 1) * sizeof(struct ext2_inode);



	if (index < 1)
		return NULL;

	if (lseek(fid, offset, SEEK_SET) != offset) {
		printf("Error: couldn't seek inode\n!");
		return NULL;
	}

	/* allocate memory which caller must free */
	inode = (struct ext2_inode*) malloc(sizeof(struct ext2_inode));
	if (!inode) {
		perror("Out of memory");
		exit(1);
	}
	if (read(fid, inode, sizeof(struct ext2_inode))
			< sizeof(struct ext2_inode)) {
		printf("Error: couldn't read inode\n!");
		return NULL;
	}
	return inode;
}

/*
 * Function:  findNameInDirBlock
 * --------------------
 * finds if a given file name exists in a directory block and returns the dir entry if found
 *
 *  block: a file block to search for the file name. the block must be one that is pointed to by an inode entry of a directory file type
 *   name: the name of the file that is being searched for in the directory
 *
 *  returns: if name is found, its directory entry is returned, otherwise NULL.
 *  			caller must free a returned not NULL dir entry
 *
 */
struct ext2_dir_entry_2 *findNameInDirBlock(char *block, char *name) {
	struct ext2_dir_entry_2 *dir;
	char *start = block;

	while (block < start + block_size) {
		dir = (struct ext2_dir_entry_2*) block;
		int len = dir->name_len > strlen(name) ? dir->name_len : strlen(name);
		if (!strncmp(name, dir->name, len)) {/*found it*/
			struct ext2_dir_entry_2 *thedir = malloc(
					sizeof(struct ext2_dir_entry_2));
			memcpy(thedir, dir, sizeof(struct ext2_dir_entry_2));
			return thedir;
		}
		block += dir->rec_len;
	}

	/* not found in this block */
	return NULL;

}

void *printDirEntriesInBlock(char *block) {
	struct ext2_dir_entry_2 *dir;
	struct ext2_inode *in;
	char *start = block;
	struct tm *timeinfo;
	char buf[80];




	while (block < start + block_size) {
		dir = (struct ext2_dir_entry_2*) block;
		in=getInodeByInodeIndex(dir->inode);

		  timeinfo = localtime(&in->i_ctime);

		  strftime(buf, sizeof(buf), "%d-%b-%Y %H:%M ", timeinfo);
		    printf("%s %s\n", buf,dir->name);

		block += dir->rec_len;

		free(in);
	}


}

/*
 * Function:  getLeafBlocks
 * ------------------------
 * finds if a given file name exists in a directory block pointed to by block ,and returns the dir entry if found
 *
 *  block: a block number pointed to by an inode entry of a directory file type
 *   name: the name of the file that is being searched for in the directory
 *
 *  returns: if name is found, its directory entry is returned, otherwise NULL.
 *  			caller must free a returned not NULL dir entry
 */
struct ext2_dir_entry_2 *getLeafBlocks(__le32 block, char *name, int mode) {
	char *realBlock;
	struct ext2_dir_entry_2 *dir=0;
	if (!block)
		return NULL;
	realBlock = malloc(block_size * sizeof(char));
	if (!realBlock) {
		perror("out of memory\n");
		exit(1);
	}
	fd_read(block, realBlock);

	if (mode==SEARCH)
		dir = findNameInDirBlock(realBlock, name);
	else
		 printDirEntriesInBlock(realBlock);
	free(realBlock);
	return dir;
}
/*
 * Function:  getIndirectBlocks
 * ------------------------
 *  reads indirect blocks and delegates to lower hierarchy indirection and leaf function to find if a given file name exists in a directory block  ,and returns the dir entry if found
 *
 *  block: an indirect block number pointed to by an inode entry of a directory file type
 *  name: the name of the file that is being searched for in the directory
 *  level: the indirection hierarchy level FIRST/SECOND/THIRD
 *
 *  returns: if name is found, its directory entry is returned, otherwise NULL.
 *  			caller must free a returned not NULL dir entry
 */
struct ext2_dir_entry_2 *getIndirectBlocks(__le32 block, char *name, int level,int mode) {
	__le32 *pointerarray;
	struct ext2_dir_entry_2 *dir;
	int i, end = block_size / sizeof(__le32 );
	/* invalid pointer */
	if (!block)
		return NULL;

	pointerarray = malloc(block_size);
	if (!pointerarray) {
		perror("out of memory\n");
		exit(1);
	}

	/* read block form disk */
	fd_read(block, pointerarray);

	for (i = 0; i < end && pointerarray[i]; i++) {
		if (level > LEAF) {
			dir = getIndirectBlocks(pointerarray[i], name, --level,mode);
		} else
			dir = getLeafBlocks(pointerarray[i], name,mode);
		if (dir) /* found it */
			return dir;
	}
	/* didn;t find it */
	return NULL;
}
/*
 * Function:  getSubDir
 * ------------------------
 * finds if a given file name exists in a directory block pointed to by block ,and returns the dir entry if found
 *
 *  block: a block number pointed to by an inode entry of a directory file type
 *   name: the name of the file that is being searched for in the directory
 *
 *  returns: if name is found, its directory entry is returned, otherwise NULL.
 *  			caller must free a returned not NULL dir entry
 */
struct ext2_dir_entry_2 *getSubDir(int inode, char* name, int mode) {

	struct ext2_inode in, *pin = getInodeByInodeIndex(inode);
	struct ext2_dir_entry_2 *dir;
	int i;

	if (!pin->i_mode & S_IFDIR) {
		printf("getSubDir error! i-node %d is not a directory\n", inode);
		free(pin);
		return NULL;
	}

	/* get rid of the pointer to indode and free its memory */
	memcpy(&in, pin, sizeof(in));
	free(pin);

	/* iterate over the first 12 nodes */
	for (i = 0; i < 12 && in.i_block[i]; i++) {
		dir = getLeafBlocks(in.i_block[i], name,mode);
		if (dir) /*found it */
			return dir;

	}

	/* the file is smaller than 12 blocks and we havn't found the name */
	if (!in.i_block[12])
		return NULL;

	dir = getIndirectBlocks(in.i_block[12], name, FIRST,mode);
	if (dir)
		return dir;

	if (!in.i_block[13])
		return NULL;

	dir = getIndirectBlocks(in.i_block[13], name, SECOND,mode);
	if (dir)
		return dir;

	if (!in.i_block[14])
		return NULL;

	return getIndirectBlocks(in.i_block[14], name, THIRD,mode);

}

__le32 isValidDirectory(char *path) {
	struct ext2_dir_entry_2 *dir;
	char s[2] = "/";
	__le32 inode;
	char *fileEntry, *path_cp = malloc(strlen(path) + 1);
	if (!path_cp) {
		perror("Out of memory");
		exit(1);
	}
	strcpy(path_cp, path);

	fileEntry = strtok(path_cp, s);

	if (!fileEntry) {
		free(path_cp);
		return 0;
	}

	/* get the first sub dir from the root dir */

	inode=EXT2_ROOT_INO;


	while (fileEntry != NULL) {

		dir = getSubDir(inode, fileEntry,SEARCH);
		if (dir->file_type!=EXT2_FT_DIR){
			free(dir);
			free(path_cp);
			return 0; /*not a directory */
		}
		inode=dir->inode;
		free(dir);
		fileEntry = strtok(NULL, s);
	}

	free(path_cp);
	return inode;
}


void printDir(__le32 inode) {
	struct ext2_dir_entry_2 *dir;
	if (!inode){
		printf("Error! invalid inode\n");
		return ;
	}
	dir = getSubDir(inode, "", PRINT);
	free(dir);
}

main() {
int d1,d2;
	struct ext2_dir_entry_2 *dir;

	fid = open("/dev/fd0", O_RDWR);


	parseSuperblock();
	parseGroupDescriptor(0);
	d1=isValidDirectory("/a/a1/a2/foo1");
	d2=isValidDirectory("/a/a1");

	printDir(d2);

	printf("d1 is %d  d2 is %d\n",d1,d2);
	dir = getSubDir(EXT2_ROOT_INO, "foo2",SEARCH);
	if (dir)
		printf(" dir found <%s>  inode %d\n", dir->name, dir->inode);
	else
		printf("Not Found!\n");

	free(dir);

	close(fid);

}
