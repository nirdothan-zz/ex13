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
 * 	  inode 1	inode 2	  inode 3  inode 4   inode 5	inode 6	 inode 7	inode 8	  inode 9  inode 10   inode 11
 *  +---------+---------+---------+---------+---------+---------+---------+---------+---------+---------+---------+
 *  |		  |         |         |         |         |         |         |         |         |         | root dir|
 *  +---------+---------+---------+---------+---------+---------+---------+---------+---------+---------+---------+
 *
 *                                                                                                      ^
 *  																						            |
 * 																			         root_inode_index * sizeof(inode)
 *
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
#define SUPERBLOCK_OFFSET 1024

static int fid, /* global variable set by the open() function */
block_size, /* bytes per sector from disk geometry */
num_block_groups, /* n= the number of block groups in the file system  */
inode_tab_block, /* the block number of the first element of the inode table */
root_inode_index; /* the index of the root dir inode */
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
	offset += (index-1) * sizeof(struct ext2_inode);

	printf("offset=%d inodesz=%d\n", offset, sizeof(struct ext2_inode));

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
struct ext2_dir_entry_2 *findNameInDirBlock(char *name, char *block){
	struct ext2_dir_entry_2 *dir;
	char *start=block;

	while (block<start+block_size){
		dir = (struct ext2_dir_entry_2*) block;
		if (!strcmp(name,dir->name)) {/*found it*/
			struct ext2_dir_entry_2 *thedir=malloc(sizeof(struct ext2_dir_entry_2));
			memcpy(thedir,dir,sizeof(dir));
			return thedir;
		}
		block+=dir->rec_len;
	}

	/* not found in this block */
	return NULL;

}

struct ext2_dir_entry_2 *getSubDir(int inode, char* name){

	struct ext2_inode *in=getInodeByInodeIndex(inode);
	struct ext2_dir_entry_2 *dir;
	int i,block_iter=0;

	if (!in->i_mode & S_IFDIR){
		printf("getSubDir error! i-node %d is not a directory\n",inode);
		free (in);
		return NULL;
	}


	/* iterate over the first 12 nodes */
	for (i = 0; i < 12 && i < in->i_blocks; i++) {
		char *buff=malloc(block_size*sizeof(char));
		if (!buff){
			perror("out of memory\n");
			exit (1);
		}
		fd_read(in->i_block[i],buff);
		if (dir=findNameInDirBlock(name,buff)){
			free(buff);
			return dir;
		}

		free(buff);
	}
	/*not found */
	return NULL;

}


main() {
	int blocks, size;
	static char buff[10240], *pBuff, *p2;
	struct ext2_dir_entry_2 *dir;
	__le16 mode;
	int i;
	static struct ext2_inode *in;
	fid = open("/dev/fd0", O_RDWR);

	printf("start\n");
	parseSuperblock();
	parseGroupDescriptor(0);

	dir=getSubDir(2,"b");

	/*in = getInodeByInodeIndex(2);

	printf(" size bytes %d blocks %d uid %d\n", in->i_size, in->i_blocks,
			in->i_uid);

	mode = in->i_mode;
	if (mode & S_IFDIR)
		printf("is Dir\n");
	else
		printf("not Dir\n");

	printf("Block[0]=%d\n", in->i_block[0]);

	pBuff = p2 = &buff;
	fd_read(in->i_block[0], pBuff);
	pBuff += block_size;
	fd_read(in->i_block[1], pBuff);
	pBuff += block_size;
	fd_read(in->i_block[2], pBuff);
	dir = (struct ext2_dir_entry_2*) p2;
	printf("name=%s  name len %d  rec len %d inode %d\n", dir->name,
			dir->name_len, dir->rec_len, dir->inode);
	p2 += dir->rec_len;
	dir = (struct ext2_dir_entry_2*) p2;
	printf("name=%s  name len %d  rec len %d inode %d\n", dir->name,
			dir->name_len, dir->rec_len, dir->inode);
	p2 += dir->rec_len;
	dir = (struct ext2_dir_entry_2*) p2;
	printf("name=%s  name len %d  rec len %d inode %d\n", dir->name,
			dir->name_len, dir->rec_len, dir->inode);
	p2 += dir->rec_len;
	dir = (struct ext2_dir_entry_2*) p2;
	printf("name=%s  name len %d  rec len %d inode %d\n", dir->name,
			dir->name_len, dir->rec_len, dir->inode);
	p2 += dir->rec_len;
	dir = (struct ext2_dir_entry_2*) p2;
	printf("name=%s  name len %d  rec len %d inode %d\n", dir->name,
			dir->name_len, dir->rec_len, dir->inode);

	in = getInodeByInodeIndex(12);

	printf(" size bytes %d blocks %d uid %d\n", in->i_size, in->i_blocks,
			in->i_uid);

	mode = in->i_mode;
	if (mode & S_IFDIR)
		printf("is Dir\n");
	else
		printf("not Dir\n");



	pBuff = p2 = &buff;
		fd_read(in->i_block[0], pBuff);
		pBuff += block_size;
		fd_read(in->i_block[1], pBuff);
		pBuff += block_size;
		fd_read(in->i_block[2], pBuff);
		dir = (struct ext2_dir_entry_2*) p2;
		printf("name=%s  name len %d  rec len %d inode %d\n", dir->name,
				dir->name_len, dir->rec_len, dir->inode);
		p2 += dir->rec_len;
		dir = (struct ext2_dir_entry_2*) p2;
		printf("name=%s  name len %d  rec len %d inode %d\n", dir->name,
				dir->name_len, dir->rec_len, dir->inode);
		p2 += dir->rec_len;
		dir = (struct ext2_dir_entry_2*) p2;
		printf("name=%s  name len %d  rec len %d inode %d\n", dir->name,
				dir->name_len, dir->rec_len, dir->inode);
		p2 += dir->rec_len;
		dir = (struct ext2_dir_entry_2*) p2;
		printf("name=%s  name len %d  rec len %d inode %d\n", dir->name,
				dir->name_len, dir->rec_len, dir->inode);
	free(in);
*/
	close(fid);

}
