/*-------------------------------------------------------------------------
 *
 * pg_clearxlogtail.c
 *	  A utility to clear ("zero out") any unused space at the end of an xlog file.
 *	  It is useful for WAL files which are switched out before they are full
 *	  because of archive_timeout, which will subsequently be compressed.
 *
 *	  This is a simple pass-through filter, operating on stdin and stdout.
 *	  It only examines the page headers, not the log records themselves.
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include <unistd.h>
#include "access/xlog_internal.h"

static const char *progname;

static void FilterOneXLOG(int rfd, int wfd);
static void ReadOneXLOGBuffer(int rfd, char *buffer, int pagesize, int continuefrom);
static void WriteOneXLOGBuffer(int wfd, char *buffer, int pagesize);

int
main(int argc, char *argv[])
{
	set_pglocale_pgservice(argv[0], "pg_clearxlogtail");
	progname = get_progname(argv[0]);
	FilterOneXLOG(fileno(stdin), fileno(stdout));
	return 0;
}

/*
 * Filter one XLOG file from stdin to stdout, clearing unused tail space.
 */
static void
FilterOneXLOG(int rfd, int wfd)
{
	char   *buffer;
	XLogPageHeader page;
	XLogLongPageHeader longpage;

	uint16		xlp_magic;		/* magic value from first page header */
	XLogRecPtr	xlp_pageaddr;	/* XLOG address of the next expected page */
	uint32		xlp_seg_size;	/* error if the stream isn't this long */
	uint32		xlp_xlog_blcksz;	/* expect a page header this often */

	int continuefrom;  /* to support peeking at the first page header */
	int nbytes;

	bool clearing = false; /* to test that we don't find a good page after first we zero */
	bool shouldbezero;     /* to test an individual page */
	char *bufzero = NULL;  /* if we find pages to zero, malloc and memset once */

	/*
	 * Allocate space to peek at the first page header.
	 * Use malloc() to ensure buffer is MAXALIGNED
	 */
	buffer = (char *) malloc(XLOG_BLCKSZ);
	if (buffer == NULL) {
		fprintf(stderr, "%s: malloc: %s\n", progname, strerror(errno));
		exit(1);
	}
	page = (XLogPageHeader) buffer;
	longpage = (XLogLongPageHeader) page;

	/* Read first page header to set things up. */
	ReadOneXLOGBuffer(rfd, buffer, SizeOfXLogLongPHD, 0);
	continuefrom = SizeOfXLogLongPHD;

	xlp_magic = page->xlp_magic;  /* Any page with a non-matching value will be cleared. */
	xlp_pageaddr = page->xlp_pageaddr;  /* Any page without expected progression will be cleared. */

	/* Warn but continue on a magic number mismatch. */
	if (xlp_magic != XLOG_PAGE_MAGIC)
		fprintf(stderr, _("%s: stdin: Warning, unexpected magic number\n"), progname);

	/* Insist that the first page header is in the long format. */
	if ((page->xlp_info & XLP_LONG_HEADER) == 0) {
		fprintf(stderr, _("%s: stdin: First page header not long format\n"), progname);
		exit(1);
	}

	xlp_seg_size = longpage->xlp_seg_size;
	xlp_xlog_blcksz = longpage->xlp_xlog_blcksz;

	/* Reallocate to the correct size, if needed. */
	if (xlp_xlog_blcksz != XLOG_BLCKSZ) {
		buffer = (char *) realloc(buffer, xlp_xlog_blcksz);
		if (buffer == NULL) {
			fprintf(stderr, "%s: realloc: %s\n", progname, strerror(errno));
			exit(1);
		}
		page = (XLogPageHeader) buffer;
	}

	for (nbytes = 0; nbytes < xlp_seg_size; nbytes += xlp_xlog_blcksz)
	{
		ReadOneXLOGBuffer(rfd, buffer, xlp_xlog_blcksz, continuefrom);
		continuefrom = 0;
		shouldbezero = (page->xlp_magic != xlp_magic ||
			page->xlp_pageaddr.xlogid != xlp_pageaddr.xlogid ||
			page->xlp_pageaddr.xrecoff != xlp_pageaddr.xrecoff);
		if (clearing && !shouldbezero) {
			fprintf(stderr, _("%s: stdin: Good page found after bad page\n"), progname);
			exit(1);
		}
		if (!clearing && shouldbezero) {
			clearing = true;
			bufzero = (char *) malloc(xlp_xlog_blcksz);
			if (bufzero == NULL) {
				fprintf(stderr, "%s: malloc: %s\n", progname, strerror(errno));
				exit(1);
			}
			memset(bufzero, 0, xlp_xlog_blcksz);
		}
		xlp_pageaddr.xrecoff += xlp_xlog_blcksz;
		WriteOneXLOGBuffer(wfd, clearing ? bufzero : buffer, xlp_xlog_blcksz);
	}
	if (read(rfd, buffer, 1) > 0) {
		fprintf(stderr, _("%s: stdin: Input longer than expected\n"), progname);
		exit(1);
	}
	if (fclose(stdout)) {
		fprintf(stderr, "%s: stdout: %s\n", progname, strerror(errno));
		exit(1);
	}
	free(buffer);
	if (clearing)
		free(bufzero);
}

static void
ReadOneXLOGBuffer(int rfd, char *buffer, int pagesize, int continuefrom)
{
	int	 nbytes;
	int  nr;
	char *pos;

	nbytes = pagesize - continuefrom;
	pos = buffer + continuefrom;
	while (nbytes > 0) {
		if ((nr = read(rfd, pos, nbytes)) < 0) {
			fprintf(stderr, "%s: stdin: %s\n", progname, strerror(errno));
			exit(1);
		}
		if (nr == 0) {
			fprintf(stderr, _("%s: stdin: Unexpected end-of-file\n"), progname);
			exit(1);
		}
		nbytes -= nr;
		pos += nr;
	}
}

static void
WriteOneXLOGBuffer(int wfd, char *buffer, int pagesize)
{
	int	 nbytes;
	int  nw;
	char *pos;

	nbytes = pagesize;
	pos = buffer;
	while (nbytes > 0) {
		if ((nw = write(wfd, pos, nbytes)) <= 0) {
			fprintf(stderr, "%s: stdout: %s\n", progname, strerror(errno));
			exit(1);
		}
		nbytes -= nw;
		pos += nw;
	}
}
