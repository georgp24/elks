#include <linuxmt/types.h>
#include <linuxmt/sched.h>
#include <linuxmt/kernel.h>
#include <linuxmt/major.h>
#include <linuxmt/string.h>
#include <linuxmt/mm.h>
#include <linuxmt/locks.h>
#include <linuxmt/errno.h>
#include <linuxmt/debug.h>

#include <arch/system.h>
#include <arch/segment.h>
#include <arch/io.h>
#include <arch/irq.h>

/*
 *	STUBS for the buffer cache when we put it in
 */
static struct buffer_head *bh_chain=NULL;
static struct buffer_head *bh_lru=NULL;
static struct buffer_head *bh_llru=NULL;

/*struct wait_queue bufwait; */	/* Wait for a free buffer */

static struct buffer_head buffers[NR_BUFFERS];
static char bufmem[NR_MAPBUFS][BLOCK_SIZE];	/* L1 buffer area */ 

#ifdef CONFIG_FS_EXTERNAL_BUFFER
static struct wait_queue bufmapwait;		/* Wait for a free L1 buffer area */
static struct buffer_head *bufmem_map[NR_MAPBUFS]; /* Array of bufmem's allocation */ 
static unsigned int _buf_ds;			/* Segment(s?) of L2 buffer cache */
#endif


/*
 *	Wait on a buffer
 */
 
void wait_on_buffer(bh)
register struct buffer_head * bh;
{
	if (buffer_locked(bh)) {

		bh->b_count++;
		
		wait_set(&bh->b_wait);

		for (;;) {
			current->state = TASK_UNINTERRUPTIBLE;
			if (!buffer_locked(bh)) {
				break;
			}
			schedule();
		}
		
		wait_clear(&bh->b_wait);
		bh->b_count--;
		current->state = TASK_RUNNING;
	}
}
/*
void lock_buffer(bh)
struct buffer_head *bh;
{
	wait_on_buffer(bh);
	bh->b_lock = 1;
	map_buffer(bh);
}
*/

static void put_last_lru(bh)
register struct buffer_head *bh;
{
	register struct buffer_head * bhn;

	if (bh_llru != bh) {
		/*
		 *	Unhook
		 */
		if((bhn = bh->b_next_lru))
			bhn->b_prev_lru=bh->b_prev_lru;
		if(bh->b_prev_lru)
			bh->b_prev_lru->b_next_lru=bhn;
		/*
		 *	Alter head
		 */
		if(bh==bh_lru)
			bh_lru=bhn;
		/*
		 *	Put on lru end
		 */
		bh->b_next_lru=NULL;
		bh->b_prev_lru=bh_llru;
		bh_llru->b_next_lru=bh;
		bh_llru=bh;
	}
}

static void sync_buffers(dev,wait)
kdev_t dev;
int wait;
{
	register struct buffer_head * bh;
	

	for(bh=bh_chain;bh!=NULL;bh=bh->b_next)
	{
		if (dev && bh->b_dev != dev) {
			continue;
		}
		/*
		 *	Skip clean buffers.
		 */
		if (buffer_clean(bh)) {
			continue;
		}
		/*
		 *	Locked buffers..
		 */
		/* Buffer is locked; skip it unless wait is
		   requested AND pass > 0. */
		if (buffer_locked(bh) && wait) {
			continue;
		} else {
			wait_on_buffer (bh);
		}
		/*
		 *	Do the stuff
		 */
		bh->b_count++;
		ll_rw_blk(WRITE, bh);
		bh->b_count--;
	}
	return;
}

void fsync_dev(dev)
kdev_t dev;
{
	sync_buffers(dev, 0);
	sync_supers(dev);
	sync_inodes(dev);
	sync_buffers(dev, 1);
}

void sync_dev(dev)
kdev_t dev;
{
	sync_buffers(dev, 0);
	sync_supers(dev);
	sync_inodes(dev);
	sync_buffers(dev, 0);
}

int sys_sync()
{
	fsync_dev(0);
	return 0;
}

void invalidate_buffers(dev)
kdev_t dev;
{
	register struct buffer_head * bh;
	
	for(bh = bh_chain; bh!=NULL; bh=bh->b_next)
	{
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev != dev)
			continue;
		if (bh->b_count)
			continue;
		bh->b_uptodate = 0;
		bh->b_dirty = 0;
		bh->b_lock = 0;
	}
}

static struct buffer_head * find_buffer(dev,block)
kdev_t dev;
block_t block;
{		
	register struct buffer_head * tmp;

	for (tmp = bh_chain ; tmp != NULL ; tmp = tmp->b_next) {
		if (tmp->b_blocknr == block && tmp->b_dev == dev)
				break;
	}
	return tmp;
}

static struct buffer_head *get_free_buffer()
{
	do {
		register struct buffer_head *bh=bh_lru;
		while(bh)
		{
#ifdef CONFIG_FS_EXTERNAL_BUFFER
			if(bh->b_count==0 && !bh->b_dirty && !bh->b_lock && !bh->b_data)
#else
			if(bh->b_count==0 && !bh->b_dirty && !bh->b_lock)
#endif
			{
				put_last_lru(bh);
				return bh;
			}
			bh=bh->b_next_lru;
		}
/*		fsync_dev(0);
		sleep_on(&bufwait); */ /* This causes a sleep until another 
					  process brelse's */
		sync_buffers(0,0);
	} while(1);
}
	
struct buffer_head * get_hash_table(dev,block)
kdev_t dev;
block_t block;
{
	register struct buffer_head * bh;

	for (;;) {
		if (!(bh=find_buffer(dev,block)))
			return NULL;
		bh->b_count++;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_blocknr == block) {
			return bh;
		}
		bh->b_count--;
	}
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algorithm is changed: hopefully better, and an elusive bug removed.
 *
 * 14.02.92: changed it to sync dirty buffers a bit: better performance
 * when the filesystem starts to get full of dirty blocks (I hope).
 */

struct buffer_head * getblk(dev,block)
kdev_t dev;
block_t block;
{
	register struct buffer_head * bh;


	/* If there are too many dirty buffers, we wake up the update process
	   now so as to ensure that there are still clean buffers available
	   for user processes to use (and dirty) */

	for (;;) {
		bh = get_hash_table(dev, block);
		if (bh != NULL) {
			if (buffer_clean(bh)) {
				if (buffer_uptodate(bh))
					 put_last_lru(bh);
			}
			return bh;
		}

		/* I think the following check is redundant *
		 * So I will remove it for now */

		if (!find_buffer(dev,block))
			break;
	} /* end for(;;) */

	/*
	 *	Create a buffer for this job.
	 */
	bh = get_free_buffer();

/* OK, FINALLY we know that this buffer is the only one of its kind, */
/* and that it's unused (b_count=0), unlocked (buffer_locked=0), and clean */
	bh->b_count=1;
	bh->b_dirty=0;
	bh->b_lock=0;
	bh->b_uptodate=0;
	bh->b_dev=dev;
	bh->b_blocknr=block;
	bh->b_seg = get_ds();
	return bh;
}

/*
 * Release a buffer head
 */

void __brelse(buf)
register struct buffer_head * buf;
{
	wait_on_buffer(buf);

	if (buf->b_count) {
		buf->b_count--;
/*		wake_up(&bufwait); */
	} else panic("brelse");
}

/*
 * bforget() is like brelse(), except it removes the buffer 
 * data validity.
 */
#if 0 
void __bforget(buf)
struct buffer_head * buf;
{
	wait_on_buffer(buf);
	buf->b_dirty = 0;
	buf->b_count--;
	buf->b_dev = NODEV;
	wake_up(&bufwait); 
}
#endif

/* Turns out both minix_bread and bread do this, so I made this a function of
 * it's own... */

struct buffer_head * readbuf(bh)
register struct buffer_head * bh;
{
	if (!buffer_uptodate(bh)) {
		ll_rw_blk(READ, bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			brelse(bh);
			bh = NULL;
		}
	}
	return bh;			
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */

struct buffer_head * bread(dev,block)
kdev_t dev;
block_t block;
{
#ifdef BLOAT_FS /* getblk never returns null */
	register struct buffer_head * bh;

	if (!(bh = getblk(dev, block))) {
		printk("VFS: bread: READ error on %s\n",
			kdevname(dev));
		return NULL;
	}
	return readbuf(bh);
#else
	return readbuf(getblk(dev, block));
#endif
}

#if 0

/* NOTHING is using breada at this point, so I can pull it out... Chad */

struct buffer_head * breada(dev,block,bufsize,
	pos, filesize)
kdev_t dev;
block_t block;
int bufsize;
unsigned int pos;
unsigned int filesize;
{
	register struct buffer_head * bh, *bha;
	int i, j;

	if (pos >= filesize)
		return NULL;

	if (block < 0 || !(bh = getblk(dev,block)))
		return NULL;

	if (buffer_uptodate(bh))
		return bh;

	bha = getblk(dev,block+1);
	if (buffer_uptodate(bha)) 
	{
		brelse(bha);
		bha=NULL;
	}
	else
	{
		/* Request the read for these buffers, and then release them */
		ll_rw_blk(READ, bha);
		brelse(bha);
	}
	/* Wait for this buffer, and then continue on */
	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		return bh;
	brelse(bh);
	return NULL;
}

#endif

void unlock_buffer(bh)
register struct buffer_head * bh;
{
	bh->b_lock = 0;
	unmap_buffer(bh);
	wake_up(&bh->b_wait);
}

void mark_buffer_uptodate(bh, on)
struct buffer_head *bh;
int on;
{
	flag_t flags;

	save_flags(flags);
	icli();
	bh->b_uptodate = on;
	restore_flags(flags);
}

#ifdef CONFIG_FS_EXTERNAL_BUFFER

static int lastumap;

/* map_buffer forces a buffer into L1 buffer space. It will freeze forever 
 * before failing, so it can return void.  This is mostly 8086 dependant, 
 * although the interface is not. */

void map_buffer(bh)
register struct buffer_head * bh;
{

    /* If buffer is already mapped, just increase the refcount and return */
    printd_bufmap2("mapping buffer %d (%d)\n", bh->b_num, bh->b_mapcount);

    if (bh->b_data) {
#if 0                           /* debugging only */
        if (!bh->b_mapcount) {
            printd_bufmap2
                ("BUFMAP: Buffer %d (block %d) `remapped' into L1.\n",
                 bh->b_num, bh->b_blocknr);
        }
#endif
        bh->b_mapcount++;
        return;
    }

    /* else keep trying till we succeed */
    for (;;) {
        int i;

        /* First check for the trivial case */
        for (i = 0; i < NR_MAPBUFS; i++) {
            if (!bufmem_map[i]) {
                /* We can just map here! */
                bufmem_map[i] = bh;
                bh->b_data = bufmem[i];
                bh->b_mapcount++;
                fmemcpy(get_ds(), bh->b_data,
                        _buf_ds, (char *) (bh->b_num * 0x400), 0x400);
                printd_bufmap3
                    ("BUFMAP: Buffer %d (block %ld) mapped into L1 slot %d.\n",
                     bh->b_num, bh->b_blocknr, i);
                return;
            }
        }

        /* Now, we check for a mapped buffer with no count and then 
         * hopefully find one to send back to L2 */
        for (i = (lastumap + 1) % NR_MAPBUFS;
             i != lastumap; i = ((i + 1) % NR_MAPBUFS)) {

            printd_bufmap1("BUFMAP: trying slot %d\n", i);

            if (!bufmem_map[i]->b_mapcount) {
                printd_bufmap1("BUFMAP: Buffer %d unmapped from L1\n",
                               bufmem_map[i]->b_num);
                /* Now unmap it */
                fmemcpy(_buf_ds,
                        (char *) (bufmem_map[i]->b_num * 0x400),
                        get_ds(), bufmem_map[i]->b_data, 0x400);
                bufmem_map[i]->b_data = 0;
                bufmem_map[i] = 0;
                break;
            }
        }

        /* The last case is to wait until unmap gets a b_mapcount down to 0 */
        if (i == lastumap) {
            /* previous loop failed */
            printd_bufmap1("BUFMAP: buffer #%d waiting on L1 slot\n",
                           bh->b_num);
            sleep_on(&bufmapwait);
            printd_bufmap("BUFMAP: wait queue woken up...\n");
        } else {
            /* success */
            lastumap = i;
        }
    }
}

/* unmap_buffer decreases bh->b_mapcount, and wakes up anyone waiting over
 * in map_buffer if it's decremented to 0... this is a bit of a misnomer,
 * since the unmapping is actually done in map_buffer to prevent frivoulous
 * unmaps if possible... */

void unmap_buffer(bh)
register struct buffer_head * bh;
{
	if (bh) {
		printd_bufmap1("unmapping buffer %d\n", bh->b_num);
		if (bh->b_mapcount <= 0) {
			printk("unmap_buffer: buffer #%x's b_mapcount<=0 already\n", bh->b_num);
			bh->b_mapcount = 0;
		} else { 
			if (!(--bh->b_mapcount)) {
/*			printd_bufmap1("BUFMAP: buffer %d released from L1.\n",
						bh->b_num);
 */				wake_up(&bufmapwait);
			}
		}
	}
	return;
}

void unmap_brelse(bh)
register struct buffer_head *bh;
{
	if (bh) {
		unmap_buffer(bh);
		__brelse(bh);
	}
}
#endif

/* This function prints the status of the L1 mappings... */
#if 0 /* Currently unused */
void print_bufmap_status()
{
	int i;

	printk("Current L1 buffer cache mappings :\n");
	printk("L1 slot / Buffer # / Reference Count\n");
	for (i = 0; i < NR_MAPBUFS; i++) {
		if (bufmem_map[i]) {
			printk("%u / %u / %u\n", i, bufmem_map[i]->b_num,
				bufmem_map[i]->b_mapcount);
		}
	}
}
#endif

void buffer_init()
{
	register struct buffer_head *bh=buffers;
	int i;

#ifdef CONFIG_FS_EXTERNAL_BUFFER
	_buf_ds = mm_alloc(NR_BUFFERS * 0x40);
	lastumap = 0;
	for (i = 0; i < NR_MAPBUFS; i++)
		bufmem_map[i] = 0;
#endif
	
	for(i=0;i<NR_BUFFERS;i++)
	{
#ifdef CONFIG_FS_EXTERNAL_BUFFER
		bh->b_data = 0;		/* L1 buffer cache is reserved! */
		bh->b_mapcount = 0;
		bh->b_num = i;		/* Used to compute L2 location */
#else
		bh->b_data = bufmem[i];
#endif
		if(i==0)
		{
			bh_chain=bh;
			bh_lru=bh;
			bh->b_prev_lru=NULL;
		}
		else
		{
			bh->b_prev_lru=bh-1;
		}
		if(i==NR_BUFFERS-1)
		{
			bh->b_next_lru=NULL;
			bh->b_next=NULL;
			bh_llru=bh;
		}
		else
		{
			bh->b_next_lru=bh+1;
			bh->b_next=bh+1;
		}
		bh++;
	}
}
