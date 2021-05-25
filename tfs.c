/*
 *  Copyright (C) 2021 CS416 Rutgers CS
 *	Tiny File System
 *	File:	tfs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "tfs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
int inoBitmapBlkNum;
int dataBitmapBlkNum;
int inoBlkNum;
int dataBlkNum;

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
	// Step 1: Read inode bitmap from disk
	char blockbuf[BLOCK_SIZE];
	bio_read(inoBitmapBlkNum, blockbuf);
	bitmap_t inoBitmap = (bitmap_t) blockbuf;
	
	// Step 2: Traverse inode bitmap to find an available slot
	// Step 3: Update inode bitmap and write to disk 
	int i;
	for (i = 0; i < MAX_INUM; i++) {
		if (get_bitmap(inoBitmap, i) == 0) {
			set_bitmap(inoBitmap, i);
			bio_write(inoBitmapBlkNum, blockbuf);
			return i;
		}
	}

	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	char blockbuf[BLOCK_SIZE];
	bio_read(dataBitmapBlkNum, blockbuf);
	bitmap_t dataBitmap = (bitmap_t) blockbuf;
	
	// Step 2: Traverse data block bitmap to find an available slot
	// Step 3: Update data block bitmap and write to disk 
	int i;
	for (i = 0; i < MAX_DNUM; i++) {
		if (get_bitmap(dataBitmap, i ) == 0) {
			set_bitmap(dataBitmap, i);
			bio_write(dataBitmapBlkNum, blockbuf);
			return i;
		}
	}


	return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
	
 
	printf("DEBUG readi got call to read inode %d\n", ino);
 
	// Step 1: Get the inode's on-disk block number
	int inodesPerBlock = BLOCK_SIZE / sizeof(struct inode);
	int blk = ino / inodesPerBlock + inoBlkNum;
 
	printf("DEBUG readi going to read data block %d\n", blk);
 
	char blockbuf[BLOCK_SIZE];
	bio_read(blk, blockbuf);

	// Step 2: Get offset of the inode in the inode on-disk block
	struct inode *iptr = (struct inode*) blockbuf + (ino % inodesPerBlock);

 
	printf("DEBUG readi got ino offset %d\n", ino % inodesPerBlock);
 
	// Step 3: Read the block from disk and then copy into inode structure
	memcpy(inode, iptr, sizeof(struct inode));
	return 0;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
 
	printf("DEBUG write got ino num %d\n", ino);
 
	int inodesPerBlock = BLOCK_SIZE / sizeof(struct inode);
	int blk = ino / inodesPerBlock + inoBlkNum;
	char blockbuf[BLOCK_SIZE];
	bio_read(blk, blockbuf);
	
	// Step 2: Get the offset in the block where this inode resides on disk
	int offset = sizeof(struct inode) * (ino % inodesPerBlock);

	// Step 3: Write inode to disk 
	memcpy(blockbuf + offset, inode, sizeof(struct inode));
	bio_write(blk, blockbuf);

	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	// Step 2: Get data block of current directory from inode
	// Step 3: Read directory's data block and check each directory entry.
	//If the name matches, then copy directory entry to dirent structure

	// make dirent buffer, read inode of current directory
	struct inode inode;
	char blockbuf[BLOCK_SIZE];
	readi(ino, &inode);

	// compute values for data block traversal
	int direntsPerBlk = BLOCK_SIZE / sizeof(struct dirent);
	// traverse data blocks of current directory's inode until match found
	struct dirent *dirp;
	int i,
	    maxDirents = direntsPerBlk * 16,
	    numDirents = inode.size / sizeof(struct dirent);
 
	printf("DEBUG starting search in dir_find at ino %d; ino size is %d; direntsPerBlk %d; maxDirents %d, direct_ptr[0] %d\n", inode.ino, inode.size, direntsPerBlk, maxDirents, inode.direct_ptr[0]);
 
	for (i = 0; i < maxDirents && numDirents; i++) {
		if (i % direntsPerBlk == 0) {
 
 
			printf("DEBUG dir_find - reading next block, block num is %d; dataBlkNum %d; inode.direct_ptr[i / direntsPerBlk] %d; i / direntsPerBlk %d\n", dataBlkNum + inode.direct_ptr[i / direntsPerBlk], dataBlkNum, inode.direct_ptr[i / direntsPerBlk], i / direntsPerBlk);
 
			bio_read(dataBlkNum + inode.direct_ptr[i / direntsPerBlk], blockbuf);
			dirp = (struct dirent*) blockbuf;
		}
 
		printf("DEBUG dir_find - numDirents %d, trying  %s\n", numDirents, dirp[i % direntsPerBlk].name);
 
		if (dirp[i % direntsPerBlk].valid) {
			numDirents--;
			if (strcmp(fname, dirp[i % direntsPerBlk].name) == 0) {
	 
				printf("DEBUG dir_find matched %s with inode # %d\n", fname, dirp[i % direntsPerBlk].ino);
	 
				memcpy(dirent, &dirp[i % direntsPerBlk], sizeof(struct dirent));
				return 0;
			}
		}
	}
	printf("DEBUG found no match in dir_find\n");
	return -ENOENT;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// Step 2: Check if fname (directory name) is already used in other entries

	char blockbuf[BLOCK_SIZE];
	struct dirent *dirp = (struct dirent*) blockbuf;
	if (dir_find(dir_inode.ino, fname, name_len, dirp) != -ENOENT) {
		return -EEXIST;
	}

	printf("DEBUG dir_add - input ino %d\n", dir_inode.ino);
	readi(dir_inode.ino, &dir_inode);
	printf("DEBUG dir_add - after readi, input ino %d\n", dir_inode.ino);
 
	int i = 0,
	    j = 0,
	    direntsPerBlk = BLOCK_SIZE / sizeof(struct dirent),
	    blksUsed = (dir_inode.size + direntsPerBlk * sizeof(struct dirent) - 1)/ (direntsPerBlk * sizeof(struct dirent));

	printf("DEBUG dir_add - dir_inode.size %d; direntsPerBlk %d, blksUsed %d\n", dir_inode.size, direntsPerBlk, blksUsed);

	// Step 3: Add directory entry in dir_inode's data block and write to disk
	if (dir_inode.size > 0) {
		for (i = 0; i < blksUsed; i++) {
			printf("DEBUG dir_add - reading block num %d, i = %d\n", dataBlkNum + dir_inode.direct_ptr[i], i);
 
			bio_read(dataBlkNum + dir_inode.direct_ptr[i], blockbuf);
			dirp = (struct dirent*) blockbuf;

			for (j = 0; j < direntsPerBlk; j++) {
			printf("DEBUG dir_add trying %s, valid is %d\n", dirp[j].name, dirp[j].valid);
 
				if (!dirp[j].valid) {
					printf("DEBUG dir_add found space at index %d of block %d\n", j, dataBlkNum + dir_inode.direct_ptr[i]);
 
					/// set new dirent
					dirp[j].ino = f_ino;
					dirp[j].valid = 1;
					memcpy(dirp[j].name, fname, name_len);
					dirp[j].name[name_len] = '\0';
					dirp[j].len = name_len;
 
					printf("DEBUG dir_add writing dirent %s to dataBlkNum + dir_inode.direct_ptr[i] %d\n", fname, dataBlkNum + dir_inode.direct_ptr[i]);
 
					// write the data block with the new dirent to disk 
					bio_write(dataBlkNum + dir_inode.direct_ptr[i], blockbuf);

					// write the updated parent dir's inode to disk 
					dir_inode.size += sizeof(struct dirent);
					dir_inode.link++;
					writei(dir_inode.ino, &dir_inode);
 
					printf("DEBUG dir_add updating dir_ino, size %d, link %d, writing to block %d\n", dir_inode.size, dir_inode.link, dir_inode.ino);
					return 0;
				}
			}
		}
		// check if, given no empty entries in existing blocks, inode is at max blocks used
		if (blksUsed == 16) {
			printf("Dir num %d at max capacity; cannot add %s\n", dir_inode.ino, fname);
			return -ENOSPC;
		}
	} else
		printf("DEBUG dir_add - got empty dir with ino %d\n", dir_inode.ino);
	printf("DEBUG dir_add - adding new data block for dirents to inode %d, fname %s, direct_ptr index %d\n", dir_inode.ino, fname, i);
 
	// Update directory inode
	printf("DEBUG dir_add - adding new data block for dirents; i %d, direct_ptr[i] %d\n", i, dir_inode.direct_ptr[i]);
	dir_inode.direct_ptr[i] = get_avail_blkno();
	memset(blockbuf, 0, BLOCK_SIZE);
	dirp = (struct dirent*) blockbuf;
	dirp->ino = f_ino;
	dirp->valid = 1;
	memcpy(dirp->name, fname, name_len);
	dirp->name[name_len] = '\0';
	dirp->len = name_len;
 
	printf("DEBUG dir_add - writing dirent with %s to data block = ino.direct_ptr[i] = %d\n", fname, dataBlkNum + dir_inode.direct_ptr[i]);
	bio_write(dataBlkNum + dir_inode.direct_ptr[i], blockbuf);

	// Write directory entry
	dir_inode.size += sizeof(struct dirent);
	dir_inode.link++;
	time(&dir_inode.vstat.st_mtime);
	writei(dir_inode.ino, &dir_inode);


	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	// Step 2: Check if fname exist
	// Step 3: If exist, then remove it from dir_inode's data block and write to disk
	int direntsPerBlk = BLOCK_SIZE / sizeof(struct dirent),
	    maxDirents = direntsPerBlk * 16,
	    numDirents = dir_inode.size / sizeof(struct dirent),
	    i;
	char blockbuf[BLOCK_SIZE];
	struct dirent *dirp;
	for (i = 0; i < maxDirents && numDirents; i++) {
		if (i % direntsPerBlk == 0) {
			bio_read(dataBlkNum + dir_inode.direct_ptr[i / direntsPerBlk], blockbuf);
			dirp = (struct dirent*) blockbuf;
		}
		if (dirp[i % direntsPerBlk].valid) {
			numDirents--;
			if (strcmp(fname, dirp[i % direntsPerBlk].name) == 0) {
				dirp[i % direntsPerBlk].valid = 0;
				bio_write(dataBlkNum + dir_inode.direct_ptr[i / direntsPerBlk], blockbuf);
				return 0;
			}
		}
	}

	return -ENOENT;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way
	// extract dirname and basename -- see 'man 3 dirname' for behavior of them
 
	printf("DEBUG GNBP - Got call to GNBP for %s, ino %d\n", path, ino);
	if (strcmp(path, "/") == 0) {
		printf("DEBUG GNBP - Got root dir, target ino %d\n", ino);
		readi(ino, inode);
		return 0;
	}
	char pathcpy1[PATH_MAX];
	char pathcpy2[PATH_MAX];
	strcpy(pathcpy1, path);
	strcpy(pathcpy2, path);
	char *dname = dirname(pathcpy1);
	char *bname = basename(pathcpy2);
	struct dirent dirp;
	// check if current dir is the root dir or "." (the current dir)
	if (strcmp(dname, "/") == 0 || strcmp(dname, ".") == 0) {
		printf("DEBUG GNBP - dname is %s; bname is %s\n", dname, bname); //debug
 
		// find the dirent for the base name in the current directory
		if (dir_find(ino, bname, strlen(bname), &dirp) == -ENOENT) {
			printf("DEBUG GNBP couldn't find base %s in dir %s\n", bname, dname);
			return -ENOENT;
		}

		printf("DEBUG GNBP - before readi: got base ino %d for path %s\n", dirp.ino, bname);
		readi(dirp.ino, inode);
		printf("DEBUG GNBP - after readi: got base ino %d for path %s\n", dirp.ino, bname);
		printf("DEBUG GNBP basecase - got valid as %d, type as %d, ino num %d, size %d, link %d, data block #1 %d\n", inode->valid, inode->type, inode->ino, inode->size, inode->link, inode->direct_ptr[0]);
		return 0;
	} else {
		// recursively evauluate the dirname
		get_node_by_path(dname, ino, inode);
		int dirInode = inode->ino;
		return get_node_by_path(bname, dirInode, inode);
	}
}

/* 
 * Make file system
 */
int tfs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	// write superblock information
	// initialize inode bitmap
	// initialize data block bitmap
	// update bitmap information for root directory
	// update inode for root directory

	char blockbuf[BLOCK_SIZE];
	dev_init(diskfile_path);
	memset(blockbuf, 0, BLOCK_SIZE);
	
	// initialize superblock
	struct superblock *sb = (struct superblock *) blockbuf;
	sb->magic_num = MAGIC_NUM;
	sb->max_inum = MAX_INUM;
	sb->max_dnum = MAX_DNUM;
	sb->i_bitmap_blk = 1;
	inoBitmapBlkNum = 1;
	sb->d_bitmap_blk = 2;
	dataBitmapBlkNum = 2;
	sb->i_start_blk = 3;
	inoBlkNum = 3;
	sb->d_start_blk = 3 + MAX_INUM * sizeof(struct inode) / BLOCK_SIZE;
	dataBlkNum = 3 + MAX_INUM * sizeof(struct inode) / BLOCK_SIZE;
	
	// write superblock to disk
	int ret = bio_write(0, blockbuf); 
 
	printf("DEBUG Got from writing superblock -- %d\n", ret);
 
	ret++; // for compiler to not complain
	
	// intialize inode and datablock bitmaps and write to disk
	memset(blockbuf, 0, BLOCK_SIZE);
	bio_write(1, blockbuf);
	bio_write(2, blockbuf);
	
	// get next free index of inode struct - should always be 0 on init
	int ind = get_avail_ino();
 
	printf("DEBUG next avail inode is %d, global blk # is %d\n", ind, inoBlkNum + ind);
 
	// initalize inode for root directory
	struct inode *ino = (struct inode*) blockbuf;
	ino->ino = ind;
	ino->valid = 1;
	ino->size = sizeof(struct dirent); // intially only contains 1 dirent: "->"
	ino->type = DIRTYPE;
	ino->link = 1;
	int i;
	for (i = 0; i < 16; i++) {
		ino->direct_ptr[i] = -1;
	}
	ino->direct_ptr[0] = get_avail_blkno();
	time(&ino->vstat.st_atime);
	time(&ino->vstat.st_mtime);

	printf("DEBUG mkfs check1 - got valid as %d, type as %d, ino as %d, size %d, link %d, data block #1 %d\n", ino->valid, ino->type, ino->ino, ino->size, ino->link, ino->direct_ptr[0]);
 
	// write updated inode-bitmap block to disk
 
	printf("DEBUG writing init inode to block %d\n", inoBlkNum + ind);
 
	bio_write(inoBlkNum + ind, blockbuf);
	memset(blockbuf, 0, BLOCK_SIZE);
	bio_read(inoBlkNum + ind, blockbuf);
	ino = (struct inode*) blockbuf;
 
	printf("DEBUG mkfs check2 - got valid as %d, type as %d, ino as %d, size %d, link %d, data block #1 %d\n", ino->valid, ino->type, ino->ino, ino->size, ino->link, ino->direct_ptr[0]);
 
	ind = ino->direct_ptr[0]; // should also be 0

	// get block num that will contain the root directory's dirents
	bio_read(ind + dataBlkNum, blockbuf);
	struct dirent *dirp = (struct dirent*) blockbuf;
	
	// set all dirents as invalid except first entry - note, struct dirent is padded to 256
	int numDirent = BLOCK_SIZE / sizeof(struct dirent);
	for (i = 1; i < numDirent; i++) {
		dirp[i].valid = 0;
	}
	dirp->ino = 0;
	dirp->valid = 1;
	memcpy(dirp->name, ".", 2);
	dirp->len = 1; 

	// write updated data block containing "." dirent of root directory back to disk
	bio_write(dataBlkNum + ind, blockbuf);
	return 0;
}


/* 
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
	if (dev_open(diskfile_path) != 0) {
		puts("calling mkfs");
		tfs_mkfs();
	}

	// Step 1b: If disk file is found, just initialize in-memory data structures
	// and read superblock from disk
	char blockbuf[BLOCK_SIZE];
	bio_read(0, blockbuf);

	struct superblock *sb = (struct superblock*) blockbuf;
	inoBitmapBlkNum = sb->i_bitmap_blk;
	dataBitmapBlkNum = sb->d_bitmap_blk;
	inoBlkNum = sb->i_start_blk;
	dataBlkNum = sb->d_start_blk;
	return NULL;
}

static void tfs_destroy(void *userdata) {
	// Step 1: De-allocate in-memory data structures
	// Step 2: Close diskfile
	dev_close();
}

static int tfs_getattr(const char *path, struct stat *stbuf) {
	// Step 1: call get_node_by_path() to get inode from path
	struct inode ino;
	printf("DEBUG getattr - called on path %s; going to call GNBP...\n", path);
 
	if (get_node_by_path(path, 0, &ino) == -ENOENT) {
		printf("DEBUG getattr - got invalid path %s\n", path);
		return -ENOENT;
	}
 
	printf("DEBUG getattr - got valid as %d, type as %d, ino as %d, size %d, link %d, data block #1 %d\n", ino.valid, ino.type, ino.ino, ino.size, ino.link, ino.direct_ptr[0]);
 
	// Step 2: fill attribute of file into stbuf from inode

	stbuf->st_dev = 0;
	stbuf->st_ino = ino.ino;
	if (ino.type == FILETYPE) {
		stbuf->st_mode   = S_IFREG | 0755;
	}
	else {
		stbuf->st_mode   = S_IFDIR | 0755;
	}
	stbuf->st_nlink = ino.link;
	stbuf->st_uid = getuid();
	stbuf->st_gid = getgid();
	stbuf->st_size = ino.size;
	stbuf->st_blksize = ino.size / BLOCK_SIZE;
	stbuf->st_blocks = ino.size / BLOCK_SIZE;
	stbuf->st_atime = ino.vstat.st_atime;
	stbuf->st_mtime = ino.vstat.st_mtime;
	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: If not find, return -1

	struct inode ino;
	if (get_node_by_path(path, 0, &ino) == -ENOENT) 
		return -1;
	time(&ino.vstat.st_atime);
	return 0;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: Read directory entries from its data blocks, and copy them to filler
	
	char blockbuf[BLOCK_SIZE];
	struct inode ino;
	if (get_node_by_path(path, 0, &ino) == -ENOENT)
		return -ENOENT;
 
	printf("DEBUG readdir - got ino %d, valid as %d, type as %d, size %d, link %d, data block #1 %d, offset %ld\n", ino.ino, ino.valid, ino.type, ino.size, ino.link, ino.direct_ptr[0], offset);
 

	int direntsPerBlk = BLOCK_SIZE / sizeof(struct dirent),
	    maxDirents = 16 * direntsPerBlk;
	int i, numDirents = ino.size / sizeof(struct dirent);
	struct dirent* dirp;
	for (i = 0; i < maxDirents && numDirents; i++) {
		if (i % direntsPerBlk == 0) {
			bio_read(ino.direct_ptr[i / direntsPerBlk] + dataBlkNum, blockbuf);
			dirp = (struct dirent*) blockbuf;
		}
 
		printf("DEBUG readdir - trying %s, valid %d, numDirents %d\n", dirp[i % direntsPerBlk].name, dirp[i % direntsPerBlk].valid, numDirents);
 
		if (dirp[i % direntsPerBlk].valid) {
			printf("DEBUG readdir sending to filler on %s matching dirent %s\n", path, dirp[i % direntsPerBlk].name);
			numDirents--;
			if (filler(buffer, dirp[i % direntsPerBlk].name, NULL, 0)) {
				return 0;
			}
		}
	}

	return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
 
	printf("DEBUG Got call to tfs_mkdir for path %s\n", path);
 
	char blockbuf[BLOCK_SIZE];
	char pathcpy1[PATH_MAX];
	char pathcpy2[PATH_MAX];
	strcpy(pathcpy1, path);
	strcpy(pathcpy2, path);
	char *dir = dirname(pathcpy1);
	char *base = basename(pathcpy2);
	int ind, ret, i;

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode inode;
	if (get_node_by_path(dir, 0, &inode) == -ENOENT)
		return -ENOENT;

	// Step 3: Call get_avail_ino() to get an available inode number
	if((ind = get_avail_ino()) > MAX_INUM) {
		return -ENOSPC;
	}

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	// Step 5: Update inode for target directory
 
	printf("DEBUG Calling dir_add to add base %s with ino num %d to parent ino num %d\n", base, ind, inode.ino);
 
	if ((ret = dir_add(inode, ind, base, strlen(base))) != 0) {
		bio_read(inoBitmapBlkNum, blockbuf);
		unset_bitmap((bitmap_t) blockbuf, ind);
		bio_write(inoBitmapBlkNum, blockbuf);
		printf("DEBUG tfs_create - couldn't add base %s to parent dir\n", base);
		return ret;
	}

	struct inode newIno;
	newIno.ino = ind;
	newIno.valid = 1;
	newIno.size = 0;
	newIno.type = DIRTYPE;
	newIno.link = 0;
	time(&newIno.vstat.st_atime);
	time(&newIno.vstat.st_mtime);
	for (i = 0; i < 16; i++) {
		newIno.direct_ptr[i] = -1;
	}
	writei(ind, &newIno);

	printf("DEBUG mkdir - set newIno members; Calling dir_add to add . and .. in newIno num %d\n", newIno.ino);
 
	if ((ret = dir_add(newIno, ind, ".", 1)) != 0) {
		bio_read(inoBitmapBlkNum, blockbuf);
		unset_bitmap((bitmap_t) blockbuf, ind);
		bio_write(inoBitmapBlkNum, blockbuf);
		return ret;
	}
	if ((ret = dir_add(newIno, ind, "..", 2)) != 0) {
		bio_read(inoBitmapBlkNum, blockbuf);
		unset_bitmap((bitmap_t) blockbuf, ind);
		bio_write(inoBitmapBlkNum, blockbuf);
		return ret;
	}
	return 0;
}

static int tfs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char blockbuf[BLOCK_SIZE];
	char pathcpy1[PATH_MAX];
	char pathcpy2[PATH_MAX];
	strcpy(pathcpy1, path);
	strcpy(pathcpy2, path);
	char *dir = dirname(pathcpy1);
	char *base = basename(pathcpy2);

	// check for invalid rmdir request
	if (strcmp(base, "..") == 0 || strcmp(base, ".") == 0) {
		printf("Invalid rmdir request; dir %s, base %s\n", dir, base);
		return -EINVAL;
	}

	// Step 2: Call get_node_by_path() to get inode of target directory
	struct inode ino;
	if (get_node_by_path(path, 0, &ino) == -ENOENT) {
		printf("DEBUG tfs_rmdir - not found - dir %s, base %s\n", dir, base);
		return -ENOENT;
	}
 
	printf("DEBUG tfs_rmdir - got base ino %d for path %s\n", ino.ino, path);
 
	// check if empty or have hard links 
	if (ino.size > 2 * sizeof(struct dirent) || ino.link > 2) {
		printf("DEBUG tfs_rmdir - not empty - dir %s, base %s; ino.size %d; ino.link %d\n", dir, base, ino.size, ino.link);
		return -ENOTEMPTY;
	}
	
	// check type
	if (ino.type != DIRTYPE) {
			printf("DEBUG tfs_rmdir - not a dir - parent dir %s unable to be removed \n", dir);
			return -ENOTDIR;
		}

	// Step 3: Clear data block bitmap of target directory
 
	printf("DEBUG tfs_rmdir - unsetting dataBitmap at ino.direct_ptr[0] = %d\n", ino.direct_ptr[0]);
	bio_read(dataBitmapBlkNum, blockbuf);
	int i;
	for (i = 0; i < 16; i++) {
		if (ino.direct_ptr[i] != -1)
			unset_bitmap((bitmap_t) blockbuf, ino.direct_ptr[0]);

	}
	bio_write(dataBitmapBlkNum, blockbuf);

	// Step 4: Clear inode bitmap and its data block
 
	printf("DEBUG tfs_rmdir - unsetting inoBitmap at target ino num %d\n", ino.ino);
 
	bio_read(inoBitmapBlkNum, blockbuf);
	unset_bitmap((bitmap_t) blockbuf, ino.ino);
	bio_write(inoBitmapBlkNum, blockbuf);
	ino.valid = 0;
	writei(ino.ino, &ino);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	if (get_node_by_path(dir, 0, &ino) == -ENOENT) {
		printf("DEBUG tfs_rmdir - not found -parent dir %s unable to be removed \n", dir);
		return -ENOENT;
	}

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	dir_remove(ino, base, strlen(base));
	ino.link--;
	ino.size -= sizeof(struct dirent);
	writei(ino.ino, &ino);

	printf("DEBUG rmdir after removal- ino %d, ino link %d, ino size %d\n", ino.ino, ino.link, ino.size);
	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char blockbuf[BLOCK_SIZE];
	char pathcpy1[PATH_MAX];
	char pathcpy2[PATH_MAX];
	strcpy(pathcpy1, path);
	strcpy(pathcpy2, path);
	char *dir = dirname(pathcpy1);
	char *base = basename(pathcpy2);
	int ind, ret;

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode ino;
	if (get_node_by_path(dir, 0, &ino) == -ENOENT) {
		printf("DEBUG tfs_create - not found - parent dir %s, base %s\n", dir, base);
		return -ENOENT;
	}

	// Step 3: Call get_avail_ino() to get an available inode number
	if((ind = get_avail_ino()) > MAX_INUM) {
		return -ENOSPC;
	}

	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	if ((ret = dir_add(ino, ind, base, strlen(base))) != 0) {
		bio_read(inoBitmapBlkNum, blockbuf);
		unset_bitmap((bitmap_t) blockbuf, ind);
		bio_write(inoBitmapBlkNum, blockbuf);
		printf("DEBUG tfs_create - couldn't add base %s to parent dir\n", base);
		return ret;
	}

	// Step 5: Update inode for target file
	struct inode newIno;
	newIno.ino = ind;
	newIno.valid = 1;
	newIno.size = 0;
	newIno.type = FILETYPE;
	newIno.link = 0;
	time(&newIno.vstat.st_atime);
	time(&newIno.vstat.st_mtime);

	// Step 6: Call writei() to write inode to disk
	writei(ind, &newIno);
	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: If not find, return -1

	struct inode ino;
	if (get_node_by_path(path, 0, &ino) != 0) {
		printf("DEBUG tfs_open - couldn't open path %s\n", path);
		return -1;
	}
	return 0;
}
static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	int read_left = size; //num of bytes left to read
	int blockNum = offset/BLOCK_SIZE;	//get the data block
	int blockOffset = offset % BLOCK_SIZE;
	struct inode * ino = malloc(sizeof(struct inode));
	get_node_by_path(path,0,ino);	
	int bytes_copied = 0;

	while(read_left > 0){
		int bytes_avail_in_block = BLOCK_SIZE - blockOffset; //num bytes left in block
		char data [BLOCK_SIZE];
		int dataBlockNumber = ino->direct_ptr[blockNum]; //get datablock num	
		bio_read(dataBlkNum + dataBlockNumber, data); //read datablock

		if(bytes_avail_in_block >= read_left){ //all byte to read are in this block
			memcpy(buffer + bytes_copied, data + blockOffset, read_left);
			bytes_copied += read_left;
			return bytes_copied;
		}
		else{
			memcpy(buffer + bytes_copied, data + blockOffset, bytes_avail_in_block);
			bytes_copied+=bytes_avail_in_block; //update how many bytes have been copied
			blockNum ++; //we need read next datablock
			blockOffset = 0; //blockoffset will always be 0 now
			read_left = read_left - bytes_avail_in_block; //update remaining bytes to read
	
		}	

	}
	return 0;
}

void allocateDataBlocks(struct inode * ino, int size, int offset){

	int currentNumBlocks = (ino->size + BLOCK_SIZE -1) / BLOCK_SIZE;//num blocks currently allocted
	int numBlocksNeeded = (size+offset + BLOCK_SIZE-1) / BLOCK_SIZE; //num blocks needed for write

	for(int i=currentNumBlocks; i<numBlocksNeeded; i++){
		ino->direct_ptr[i] = get_avail_blkno();
	}
	
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	int write_left = size; //num of bytes left to read
	int blockNum = offset/BLOCK_SIZE;	//get the data block
	int blockOffset = offset % BLOCK_SIZE;
	struct inode * ino = malloc(sizeof(struct inode));
	get_node_by_path(path,0,ino);		
	int bytes_copied = 0;

	allocateDataBlocks(ino,size,offset);		//determine how many data blocks we need to allocate

	while(write_left >0){
		int bytes_avail_in_block = BLOCK_SIZE - blockOffset; //num bytes left in block
		char data [BLOCK_SIZE];
		int dataBlockNumber = ino->direct_ptr[blockNum]; //get datablock num	
		bio_read(dataBlkNum + dataBlockNumber, data); //read datablock

		if(bytes_avail_in_block >= write_left){ //all bytes left to write are in this block
			memcpy(data + blockOffset, buffer + bytes_copied, write_left);
			bytes_copied += write_left;
			bio_write(dataBlkNum + dataBlockNumber, data); //write datablock
			break;
		}
		else{
			memcpy(data + blockOffset,buffer + bytes_copied, bytes_avail_in_block); //copy into data block from buffer
			bytes_copied+=bytes_avail_in_block; //update how many bytes have been copied
			bio_write(dataBlkNum + dataBlockNumber, data); //write datablock			
			blockNum ++; //we need read next datablock
			blockOffset = 0; //blockoffset will always be 0 now
			write_left = write_left - bytes_avail_in_block; //update remaining bytes to read
		
		}	


	}
	if(offset + bytes_copied > ino->size){
		ino->size = bytes_copied + offset;
	}
	//ino->size += bytes_copied;
	writei(ino->ino,ino);
	return bytes_copied;
}
static int tfs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char blockbuf[BLOCK_SIZE];
	char pathcpy1[PATH_MAX];
	char pathcpy2[PATH_MAX];
	strcpy(pathcpy1, path);
	strcpy(pathcpy2, path);
	char *dir = dirname(pathcpy1);
	char *base = basename(pathcpy2);

	// Step 2: Call get_node_by_path() to get inode of target file
	struct inode ino;
	int ret;
	if ((ret = get_node_by_path(path, 0, &ino)) != 0) {
		printf("DEBUG tfs_unlink - path %s got error: %d\n", path, ret);
		return ret;
	}

	// Step 3: Clear data block bitmap of target file
	bio_read(dataBitmapBlkNum, blockbuf);
	int blksUsed = (ino.size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	while (blksUsed-- > 0) {
		unset_bitmap((bitmap_t) blockbuf, ino.direct_ptr[blksUsed]);
	}
	bio_write(dataBitmapBlkNum, blockbuf);

	// Step 4: Clear inode bitmap and its data block
	bio_read(inoBitmapBlkNum, blockbuf);
	unset_bitmap((bitmap_t) blockbuf, ino.ino);
	bio_write(inoBitmapBlkNum, blockbuf);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	if ((ret = get_node_by_path(dir, 0, &ino)) != 0) {
		printf("DEBUG tfs_unlink - parent dir %s got error: %d\n", dir, ret);
		return ret;
	}

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory
	if ((ret = dir_remove(ino, base, strlen(base))) != 0) {
		printf("DEBUG tfs_unlink - parent dir %s got error: %d on dir_remove\n", dir, ret);
		return ret;
	}
	ino.size -= sizeof(struct dirent);
	ino.link--;
	writei(ino.ino, &ino);
	return 0;
}

static int tfs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int tfs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations tfs_ope = {
	.init		= tfs_init,
	.destroy	= tfs_destroy,

	.getattr	= tfs_getattr,
	.readdir	= tfs_readdir,
	.opendir	= tfs_opendir,
	.releasedir	= tfs_releasedir,
	.mkdir		= tfs_mkdir,
	.rmdir		= tfs_rmdir,

	.create		= tfs_create,
	.open		= tfs_open,
	.read 		= tfs_read,
	.write		= tfs_write,
	.unlink		= tfs_unlink,

	.truncate   = tfs_truncate,
	.flush      = tfs_flush,
	.utimens    = tfs_utimens,
	.release	= tfs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &tfs_ope, NULL);

	return fuse_stat;
}

