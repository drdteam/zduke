// "Build Engine & Tools" Copyright (c) 1993-1997 Ken Silverman
// Ken Silverman's official web site: "http://www.advsys.net/ken"
// See the included license file "BUILDLIC.TXT" for license info.

#include <stdlib.h>
#include <dos.h>
#include <stdio.h>
#include <ctype.h>
#include "pragmas.h"
#include "buildprotos.h"

//   This module keeps track of a standard linear cacheing system.
//   To use this module, here's all you need to do:
//
//   Step 1: Allocate a nice BIG buffer, like from 1MB-4MB and
//           Call initcache(long cachestart, long cachesize) where
//
//              cachestart = (long)(pointer to start of BIG buffer)
//              cachesize = length of BIG buffer
//
//   Step 2: Call allocache(long *bufptr, long bufsiz, char *lockptr)
//              whenever you need to allocate a buffer, where:
//
//              *bufptr = pointer to 4-byte pointer to buffer
//                 Confused?  Using this method, cache2d can remove
//                 previously allocated things from the cache safely by
//                 setting the 4-byte pointer to 0.
//              bufsiz = number of bytes to allocate
//              *lockptr = pointer to locking char which tells whether
//                 the region can be removed or not.  If *lockptr = 0 then
//                 the region is not locked else its locked.
//
//   Step 3: If you need to remove everything from the cache, or every
//           unlocked item from the cache, you can call uninitcache();
//              Call uninitcache(0) to remove all unlocked items, or
//              Call uninitcache(1) to remove everything.
//           After calling uninitcache, it is still ok to call allocache
//           without first calling initcache.

#define MAXCACHEOBJECTS 9216

typedef unsigned char BYTE;
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

static long cachesize = 0;
long cachecount = 0;
char zerochar = 0;
char *cachestart = NULL;
long cacnum = 0, agecount = 0;
typedef struct { void **hand; long leng; unsigned char *lock; } cactype;
cactype cac[MAXCACHEOBJECTS];
static long lockrecip[200];

void initcache(char *dacachestart, long dacachesize)
{
	long i;

	for(i=1;i<200;i++) lockrecip[i] = (1<<28)/(200-i);

	cachestart = dacachestart;
	cachesize = dacachesize;

	cac[0].leng = (long)cachesize;
	cac[0].lock = (BYTE *)&zerochar;
	cacnum = 1;
}

void allocache (void **newhandle, long newbytes, unsigned char *newlockptr)
{
	long i, z, zz, bestz, daval, bestval, besto, o1, o2, sucklen, suckz;

	newbytes = ((newbytes+15)&0xfffffff0);

	if ((unsigned)newbytes > (unsigned)cachesize)
	{
		Printf("Cachesize: %ld\n",cachesize);
		Printf("*Newhandle: 0x%x, Newbytes: %ld, *Newlock: %d\n",newhandle,newbytes,*newlockptr);
		reportandexit("BUFFER TOO BIG TO FIT IN CACHE!");
	}

	if (*newlockptr == 0)
	{
		reportandexit("ALLOCACHE CALLED WITH LOCK OF 0!");
	}

		//Find best place
	bestval = 0x7fffffff; o1 = cachesize;
	for(z=cacnum-1;z>=0;z--)
	{
		o1 -= cac[z].leng;
		o2 = o1+newbytes; if (o2 > cachesize) continue;

		daval = 0;
		for(i=o1,zz=z;i<o2;i+=cac[zz++].leng)
		{
			if (*cac[zz].lock == 0) continue;
			if (*cac[zz].lock >= 200) { daval = 0x7fffffff; break; }
			daval += mulscale32(cac[zz].leng+65536,lockrecip[*cac[zz].lock]);
			if (daval >= bestval) break;
		}
		if (daval < bestval)
		{
			bestval = daval; besto = o1; bestz = z;
			if (bestval == 0) break;
		}
	}

	//Printf("%ld %ld %ld\n",besto,newbytes,*newlockptr);

	if (bestval == 0x7fffffff)
		reportandexit("CACHE SPACE ALL LOCKED UP!");

		//Suck things out
	for(sucklen=-newbytes,suckz=bestz;sucklen<0;sucklen+=cac[suckz++].leng)
		if (*cac[suckz].lock) *cac[suckz].hand = 0;

		//Remove all blocks except 1
	suckz -= (bestz+1); cacnum -= suckz;
	copybufbyte(&cac[bestz+suckz],&cac[bestz],(cacnum-bestz)*sizeof(cactype));
	*newhandle = cachestart+besto;
	cac[bestz].hand = newhandle;
	cac[bestz].leng = newbytes;
	cac[bestz].lock = newlockptr;
	cachecount++;

		//Add new empty block if necessary
	if (sucklen <= 0) return;

	bestz++;
	if (bestz == cacnum)
	{
		cacnum++; if (cacnum > MAXCACHEOBJECTS) reportandexit("Too many objects in cache! (cacnum > MAXCACHEOBJECTS)");
		cac[bestz].leng = sucklen;
		cac[bestz].lock = (BYTE *)&zerochar;
		return;
	}

	if (*cac[bestz].lock == 0) { cac[bestz].leng += sucklen; return; }

	cacnum++; if (cacnum > MAXCACHEOBJECTS) reportandexit("Too many objects in cache! (cacnum > MAXCACHEOBJECTS)");
	for(z=cacnum-1;z>bestz;z--) cac[z] = cac[z-1];
	cac[bestz].leng = sucklen;
	cac[bestz].lock = (BYTE *)&zerochar;
}

void suckcache (long *suckptr)
{
	long i;

		//Can't exit early, because invalid pointer might be same even though lock = 0
	for(i=0;i<cacnum;i++)
		if ((long)(*cac[i].hand) == (long)suckptr)
		{
			if (*cac[i].lock) *cac[i].hand = 0;
			cac[i].lock = (BYTE *)&zerochar;
			cac[i].hand = 0;

				//Combine empty blocks
			if ((i > 0) && (*cac[i-1].lock == 0))
			{
				cac[i-1].leng += cac[i].leng;
				cacnum--; copybuf(&cac[i+1],&cac[i],(cacnum-i)*sizeof(cactype));
			}
			else if ((i < cacnum-1) && (*cac[i+1].lock == 0))
			{
				cac[i+1].leng += cac[i].leng;
				cacnum--; copybuf(&cac[i+1],&cac[i],(cacnum-i)*sizeof(cactype));
			}
		}
}

void agecache(void)
{
	long cnt;
	char ch;

	if (agecount >= cacnum) agecount = cacnum-1;
	for(cnt=(cacnum>>4);cnt>=0;cnt--)
	{
		ch = (*cac[agecount].lock);
		if (((ch-2)&255) < 198)
			(*cac[agecount].lock) = ch-1;

		agecount--; if (agecount < 0) agecount = cacnum-1;
	}
}

void reportandexit(char *errormessage)
{
	long i, j;

#ifdef OKAYFIXED
	setvmode(0x3);
#endif
	j = 0;
	for(i=0;i<cacnum;i++)
	{
		Printf("%ld- ",i);
		Printf("ptr: 0x%x, ",*cac[i].hand);
		Printf("leng: %ld, ",cac[i].leng);
		Printf("lock: %ld\n",*cac[i].lock);
		j += cac[i].leng;
	}
	Printf("Cachesize = %ld\n",cachesize);
	Printf("Cacnum = %ld\n",cacnum);
	Printf("Cache length sum = %ld\n",j);
	Printf("ERROR: %s",errormessage);
	exit(0);
}




#include <fcntl.h>
#include <io.h>
#include <sys\types.h>
#include <sys\stat.h>

#define MAXGROUPFILES 4     //Warning: Fix groupfil if this is changed
#define MAXOPENFILES 64     //Warning: Fix filehan if this is changed

static long numgroupfiles = 0;
static long gnumfiles[MAXGROUPFILES];
static long groupfil[MAXGROUPFILES] = {-1,-1,-1,-1};
static long groupfilpos[MAXGROUPFILES];
static char *gfilelist[MAXGROUPFILES];
static long *gfileoffs[MAXGROUPFILES];

static unsigned char filegrp[MAXOPENFILES];
static long filepos[MAXOPENFILES];
static long filehan[MAXOPENFILES] =
{
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

long initgroupfile(char *filename)
{
	char buf[16];
	long i, j, k;

	if (numgroupfiles >= MAXGROUPFILES) return(-1);

	groupfil[numgroupfiles] = open(filename,O_BINARY|O_RDWR,S_IREAD);
	if (groupfil[numgroupfiles] != -1)
	{
		groupfilpos[numgroupfiles] = 0;
		read(groupfil[numgroupfiles],buf,16);
		if ((buf[0] != 'K') || (buf[1] != 'e') || (buf[2] != 'n') ||
			 (buf[3] != 'S') || (buf[4] != 'i') || (buf[5] != 'l') ||
			 (buf[6] != 'v') || (buf[7] != 'e') || (buf[8] != 'r') ||
			 (buf[9] != 'm') || (buf[10] != 'a') || (buf[11] != 'n'))
		{
			close(groupfil[numgroupfiles]);
			groupfil[numgroupfiles] = -1;
			return(-1);
		}
		gnumfiles[numgroupfiles] = *((long *)&buf[12]);

		if ((gfilelist[numgroupfiles] = (char *)kmalloc(gnumfiles[numgroupfiles]<<4)) == 0)
			{ Printf("Not enough memory for file grouping system\n"); exit(0); }
		if ((gfileoffs[numgroupfiles] = (long *)kmalloc((gnumfiles[numgroupfiles]+1)<<2)) == 0)
			{ Printf("Not enough memory for file grouping system\n"); exit(0); }

		read(groupfil[numgroupfiles],gfilelist[numgroupfiles],gnumfiles[numgroupfiles]<<4);

		j = 0;
		for(i=0;i<gnumfiles[numgroupfiles];i++)
		{
			k = *((long *)&gfilelist[numgroupfiles][(i<<4)+12]);
			gfilelist[numgroupfiles][(i<<4)+12] = 0;
			gfileoffs[numgroupfiles][i] = j;
			j += k;
		}
		gfileoffs[numgroupfiles][gnumfiles[numgroupfiles]] = j;
	}
	numgroupfiles++;
	return(groupfil[numgroupfiles-1]);
}

void uninitgroupfile(void)
{
	long i;

	for(i=numgroupfiles-1;i>=0;i--)
		if (groupfil[i] != -1)
		{
			kfree(gfilelist[i]);
			kfree(gfileoffs[i]);
			close(groupfil[i]);
			groupfil[i] = -1;
		}
}

long kopen4load(const char *filename, char searchfirst)
{
	long i, j, k, fil, newhandle;
	char bad, *gfileptr;

	newhandle = MAXOPENFILES-1;
	while (filehan[newhandle] != -1)
	{
		newhandle--;
		if (newhandle < 0)
		{
			Printf("TOO MANY FILES OPEN IN FILE GROUPING SYSTEM!");
			exit(0);
		}
	}

	if (searchfirst == 0)
		if ((fil = open(filename,O_BINARY|O_RDONLY)) != -1)
		{
			filegrp[newhandle] = 255;
			filehan[newhandle] = fil;
			filepos[newhandle] = 0;
			return(newhandle);
		}
	for(k=numgroupfiles-1;k>=0;k--)
	{
		if (searchfirst != 0) k = 0;
		if (groupfil[k] != -1)
		{
			for(i=gnumfiles[k]-1;i>=0;i--)
			{
				gfileptr = (char *)&gfilelist[k][i<<4];

				bad = 0;
				for(j=0;j<13;j++)
				{
					if (!filename[j]) break;
					if (toupper(filename[j]) != toupper(gfileptr[j]))
						{ bad = 1; break; }
				}
				if (bad) continue;

				filegrp[newhandle] = (unsigned char)k;
				filehan[newhandle] = i;
				filepos[newhandle] = 0;
				return(newhandle);
			}
		}
	}
	return(-1);
}

long kread(long handle, void *buffer, long leng)
{
	long i, filenum, groupnum;

	filenum = filehan[handle];
	groupnum = filegrp[handle];
	if (groupnum == 255) return(read(filenum,buffer,leng));

	if (groupfil[groupnum] != -1)
	{
		i = gfileoffs[groupnum][filenum]+filepos[handle];
		if (i != groupfilpos[groupnum])
		{
			lseek(groupfil[groupnum],i+((gnumfiles[groupnum]+1)<<4),SEEK_SET);
			groupfilpos[groupnum] = i;
		}
		leng = min(leng,(gfileoffs[groupnum][filenum+1]-gfileoffs[groupnum][filenum])-filepos[handle]);
		leng = read(groupfil[groupnum],buffer,leng);
		filepos[handle] += leng;
		groupfilpos[groupnum] += leng;
		return(leng);
	}

	return(0);
}

long klseek(long handle, long offset, long whence)
{
	long i, groupnum;

	groupnum = filegrp[handle];

	if (groupnum == 255) return(lseek(filehan[handle],offset,whence));
	if (groupfil[groupnum] != -1)
	{
		switch(whence)
		{
			case SEEK_SET: filepos[handle] = offset; break;
			case SEEK_END: i = filehan[handle];
								filepos[handle] = (gfileoffs[groupnum][i+1]-gfileoffs[groupnum][i])+offset;
								break;
			case SEEK_CUR: filepos[handle] += offset; break;
		}
		return(filepos[handle]);
	}
	return(-1);
}

long ktell(long handle)	// [RH] added this for FMOD
{
	long groupnum;

	groupnum = filegrp[handle];

	if (groupnum == 255) return tell(filehan[handle]);
	return filepos[handle];
}

long kfilelength(long handle)
{
	long i, groupnum;

	groupnum = filegrp[handle];
	if (groupnum == 255) return(filelength(filehan[handle]));
	i = filehan[handle];
	return(gfileoffs[groupnum][i+1]-gfileoffs[groupnum][i]);
}

void kclose(long handle)
{
	if (handle < 0) return;
	if (filegrp[handle] == 255) close(filehan[handle]);
	filehan[handle] = -1;
}




	//Internal LZW variables
#define LZWSIZE 16384           //Watch out for shorts!
static char *lzwbuf1, *lzwbuf4, *lzwbuf5;
static unsigned char lzwbuflock[5];
static short *lzwbuf2, *lzwbuf3;

void kdfread(void *buffer, size_t dasizeof, size_t count, long fil)
{
	long k, kgoal;
	size_t i, j;
	short leng;
	char *ptr;

	lzwbuflock[0] = lzwbuflock[1] = lzwbuflock[2] = lzwbuflock[3] = lzwbuflock[4] = 200;
	if (lzwbuf1 == NULL) allocache((void**)&lzwbuf1,LZWSIZE+(LZWSIZE>>4),&lzwbuflock[0]);
	if (lzwbuf2 == NULL) allocache((void**)&lzwbuf2,(LZWSIZE+(LZWSIZE>>4))*2,&lzwbuflock[1]);
	if (lzwbuf3 == NULL) allocache((void**)&lzwbuf3,(LZWSIZE+(LZWSIZE>>4))*2,&lzwbuflock[2]);
	if (lzwbuf4 == NULL) allocache((void**)&lzwbuf4,LZWSIZE,&lzwbuflock[3]);
	if (lzwbuf5 == NULL) allocache((void**)&lzwbuf5,LZWSIZE+(LZWSIZE>>4),&lzwbuflock[4]);

	if (dasizeof > LZWSIZE) { count *= dasizeof; dasizeof = 1; }
	ptr = (char *)buffer;

	kread(fil,&leng,2); kread(fil,lzwbuf5,(long)leng);
	k = 0; kgoal = uncompress(lzwbuf5,(long)leng,lzwbuf4);

	copybufbyte(lzwbuf4,ptr,(long)dasizeof);
	k += (long)dasizeof;

	for(i=1;i<count;i++)
	{
		if (k >= kgoal)
		{
			kread(fil,&leng,2); kread(fil,lzwbuf5,(long)leng);
			k = 0; kgoal = uncompress(lzwbuf5,(long)leng,lzwbuf4);
		}
		for(j=0;j<dasizeof;j++) ptr[j+dasizeof] = ((ptr[j]+lzwbuf4[j+k])&255);
		k += dasizeof;
		ptr += dasizeof;
	}
	lzwbuflock[0] = lzwbuflock[1] = lzwbuflock[2] = lzwbuflock[3] = lzwbuflock[4] = 1;
}

void dfread(void *buffer, size_t dasizeof, size_t count, FILE *fil)
{
	size_t i, j;
	long k, kgoal;
	short leng;
	char *ptr;

	lzwbuflock[0] = lzwbuflock[1] = lzwbuflock[2] = lzwbuflock[3] = lzwbuflock[4] = 200;
	if (lzwbuf1 == NULL) allocache((void**)&lzwbuf1,LZWSIZE+(LZWSIZE>>4),&lzwbuflock[0]);
	if (lzwbuf2 == NULL) allocache((void**)&lzwbuf2,(LZWSIZE+(LZWSIZE>>4))*2,&lzwbuflock[1]);
	if (lzwbuf3 == NULL) allocache((void**)&lzwbuf3,(LZWSIZE+(LZWSIZE>>4))*2,&lzwbuflock[2]);
	if (lzwbuf4 == NULL) allocache((void**)&lzwbuf4,LZWSIZE,&lzwbuflock[3]);
	if (lzwbuf5 == NULL) allocache((void**)&lzwbuf5,LZWSIZE+(LZWSIZE>>4),&lzwbuflock[4]);

	if (dasizeof > LZWSIZE) { count *= dasizeof; dasizeof = 1; }
	ptr = (char *)buffer;

	fread(&leng,2,1,fil); fread(lzwbuf5,(long)leng,1,fil);
	k = 0; kgoal = uncompress(lzwbuf5,(long)leng,lzwbuf4);

	copybufbyte(lzwbuf4,ptr,(long)dasizeof);
	k += (long)dasizeof;

	for(i=1;i<count;i++)
	{
		if (k >= kgoal)
		{
			fread(&leng,2,1,fil); fread(lzwbuf5,(long)leng,1,fil);
			k = 0; kgoal = uncompress(lzwbuf5,(long)leng,lzwbuf4);
		}
		for(j=0;j<dasizeof;j++) ptr[j+dasizeof] = ((ptr[j]+lzwbuf4[j+k])&255);
		k += dasizeof;
		ptr += dasizeof;
	}
	lzwbuflock[0] = lzwbuflock[1] = lzwbuflock[2] = lzwbuflock[3] = lzwbuflock[4] = 1;
}

void dfwrite(void *buffer, size_t dasizeof, size_t count, FILE *fil)
{
	size_t i, j, k;
	short leng;
	char *ptr;

	lzwbuflock[0] = lzwbuflock[1] = lzwbuflock[2] = lzwbuflock[3] = lzwbuflock[4] = 200;
	if (lzwbuf1 == NULL) allocache((void**)&lzwbuf1,LZWSIZE+(LZWSIZE>>4),&lzwbuflock[0]);
	if (lzwbuf2 == NULL) allocache((void**)&lzwbuf2,(LZWSIZE+(LZWSIZE>>4))*2,&lzwbuflock[1]);
	if (lzwbuf3 == NULL) allocache((void**)&lzwbuf3,(LZWSIZE+(LZWSIZE>>4))*2,&lzwbuflock[2]);
	if (lzwbuf4 == NULL) allocache((void**)&lzwbuf4,LZWSIZE,&lzwbuflock[3]);
	if (lzwbuf5 == NULL) allocache((void**)&lzwbuf5,LZWSIZE+(LZWSIZE>>4),&lzwbuflock[4]);

	if (dasizeof > LZWSIZE) { count *= dasizeof; dasizeof = 1; }
	ptr = (char *)buffer;

	copybufbyte(ptr,lzwbuf4,(long)dasizeof);
	k = dasizeof;

	if (k > LZWSIZE-dasizeof)
	{
		leng = (short)compress(lzwbuf4,k,lzwbuf5); k = 0;
		fwrite(&leng,2,1,fil); fwrite(lzwbuf5,(long)leng,1,fil);
	}

	for(i=1;i<count;i++)
	{
		for(j=0;j<dasizeof;j++) lzwbuf4[j+k] = ((ptr[j+dasizeof]-ptr[j])&255);
		k += dasizeof;
		if (k > LZWSIZE-dasizeof)
		{
			leng = (short)compress(lzwbuf4,k,lzwbuf5); k = 0;
			fwrite(&leng,2,1,fil); fwrite(lzwbuf5,(long)leng,1,fil);
		}
		ptr += dasizeof;
	}
	if (k > 0)
	{
		leng = (short)compress(lzwbuf4,k,lzwbuf5);
		fwrite(&leng,2,1,fil); fwrite(lzwbuf5,(long)leng,1,fil);
	}
	lzwbuflock[0] = lzwbuflock[1] = lzwbuflock[2] = lzwbuflock[3] = lzwbuflock[4] = 1;
}

long compress(char *lzwinbuf, long uncompleng, char *lzwoutbuf)
{
	long i, addr, newaddr, addrcnt, zx, *longptr;
	long bytecnt1, bitcnt, numbits, oneupnumbits;
	short *shortptr;

	for(i=255;i>=0;i--) { lzwbuf1[i] = (char)i; lzwbuf3[i] = (short)(i+1)&255; }
	clearbuf(FP_OFF(lzwbuf2),256>>1,0xffffffff);
	clearbuf(FP_OFF(lzwoutbuf),((uncompleng+15)+3)>>2,0L);

	addrcnt = 256; bytecnt1 = 0; bitcnt = (4<<3);
	numbits = 8; oneupnumbits = (1<<8);
	do
	{
		addr = lzwinbuf[bytecnt1];
		do
		{
			bytecnt1++;
			if (bytecnt1 == uncompleng) break;
			if (lzwbuf2[addr] < 0) {lzwbuf2[addr] = (short)addrcnt; break;}
			newaddr = lzwbuf2[addr];
			while (lzwbuf1[newaddr] != lzwinbuf[bytecnt1])
			{
				zx = lzwbuf3[newaddr];
				if (zx < 0) {lzwbuf3[newaddr] = (short)addrcnt; break;}
				newaddr = zx;
			}
			if (lzwbuf3[newaddr] == addrcnt) break;
			addr = newaddr;
		} while (addr >= 0);
		lzwbuf1[addrcnt] = lzwinbuf[bytecnt1];
		lzwbuf2[addrcnt] = -1;
		lzwbuf3[addrcnt] = -1;

		longptr = (long *)&lzwoutbuf[bitcnt>>3];
		longptr[0] |= (addr<<(bitcnt&7));
		bitcnt += numbits;
		if ((addr&((oneupnumbits>>1)-1)) > ((addrcnt-1)&((oneupnumbits>>1)-1)))
			bitcnt--;

		addrcnt++;
		if (addrcnt > oneupnumbits) { numbits++; oneupnumbits <<= 1; }
	} while ((bytecnt1 < uncompleng) && (bitcnt < (uncompleng<<3)));

	longptr = (long *)&lzwoutbuf[bitcnt>>3];
	longptr[0] |= (addr<<(bitcnt&7));
	bitcnt += numbits;
	if ((addr&((oneupnumbits>>1)-1)) > ((addrcnt-1)&((oneupnumbits>>1)-1)))
		bitcnt--;

	shortptr = (short *)lzwoutbuf;
	shortptr[0] = (short)uncompleng;
	if (((bitcnt+7)>>3) < uncompleng)
	{
		shortptr[1] = (short)addrcnt;
		return((bitcnt+7)>>3);
	}
	shortptr[1] = (short)0;
	for(i=0;i<uncompleng;i++) lzwoutbuf[i+4] = lzwinbuf[i];
	return(uncompleng+4);
}

long uncompress(char *lzwinbuf, long compleng, char *lzwoutbuf)
{
	long strtot, currstr, numbits, oneupnumbits;
	long i, dat, leng, bitcnt, outbytecnt, *longptr;
	short *shortptr;

	shortptr = (short *)lzwinbuf;
	strtot = (long)shortptr[1];
	if (strtot == 0)
	{
		copybuf(FP_OFF(lzwinbuf)+4,FP_OFF(lzwoutbuf),((compleng-4)+3)>>2);
		return((long)shortptr[0]); //uncompleng
	}
	for(i=255;i>=0;i--) { lzwbuf2[i] = (short)i; lzwbuf3[i] = (short)i; }
	currstr = 256; bitcnt = (4<<3); outbytecnt = 0;
	numbits = 8; oneupnumbits = (1<<8);
	do
	{
		longptr = (long *)&lzwinbuf[bitcnt>>3];
		dat = ((longptr[0]>>(bitcnt&7)) & (oneupnumbits-1));
		bitcnt += numbits;
		if ((dat&((oneupnumbits>>1)-1)) > ((currstr-1)&((oneupnumbits>>1)-1)))
			{ dat &= ((oneupnumbits>>1)-1); bitcnt--; }

		lzwbuf3[currstr] = (short)dat;

		for(leng=0;dat>=256;leng++,dat=lzwbuf3[dat])
			lzwbuf1[leng] = (char)lzwbuf2[dat];

		lzwoutbuf[outbytecnt++] = (char)dat;
		for(i=leng-1;i>=0;i--) lzwoutbuf[outbytecnt++] = lzwbuf1[i];

		lzwbuf2[currstr-1] = (short)dat; lzwbuf2[currstr] = (short)dat;
		currstr++;
		if (currstr > oneupnumbits) { numbits++; oneupnumbits <<= 1; }
	} while (currstr < strtot);
	return((long)shortptr[0]); //uncompleng
}
