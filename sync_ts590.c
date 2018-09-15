/*
 * Copyright (c) 2017
 *      Tamotsu Kanoh <kanoh@kanoh.org>. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither my name nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include "sync_ts590.h"

#ifndef TTY_DEV0
#define	TTY_DEV0	"/dev/ttyU0"
#endif

#ifndef TTY_DEV1
#define	TTY_DEV1	"/dev/ttyU1"
#endif

#ifndef B_TS590
#define B_TS590		B9600
#endif

int fd[2];
struct termios term_def[2];
char	*cmd_buf;

void proc_abort(int sig)
{
	int i;
	char buf[BUFSIZ];

	for(i=0;i<2;i++) {
		write(fd[i],"AI0;",4);
		tcflush(fd[i],TCIOFLUSH);
		tcsetattr(fd[i], TCSANOW, &term_def[i]);
		close(fd[i]);
	}

	fprintf(stderr,"\nSyncing TS-590 closed by signal %d\n", sig);

	exit(0);
}

int init_signal()
{
	signal(SIGINT,proc_abort);
	signal(SIGTERM,proc_abort);
	signal(SIGQUIT,proc_abort);
	signal(SIGHUP,proc_abort);

	signal(SIGPIPE,SIG_IGN);

	return(0);
}

char *read_cmd(int _fd)
{
	unsigned char c;
	char *buf_p;

	memset(cmd_buf, 0, BUFSIZ); 
	buf_p = cmd_buf;

	do {
		read(_fd,(char *)&c,1);
		if(c != '?')
			*buf_p++ = c;
	} while(c != ';');

	return(cmd_buf);
}

char *write_cmd(int _fd, char *cmd)
{
	char *buf;

	write(_fd,cmd,strlen(cmd));
	buf = read_cmd(_fd);
	tcflush(_fd,TCIOFLUSH);
	return(buf);
}

void usages(buf)
char *buf;
{
	printf("usages: %s [-0 tty_dev] [-1 tty_dev]\n",buf);
	exit(0);
}

int main(argc, argv)
int argc;
char **argv;
{
	int i, j, n, wd, rc, iFlags[2];
	struct termios term[2];
	struct timeval timeout;
	fd_set rd, mask;
	char c, *buf, *msg;

	int g_series = -1;
	char *tty_dev[] = { TTY_DEV0, TTY_DEV1 };

	while ((i = getopt(argc, argv, "0:1:h?")) != -1) {
		switch (i) {
			case '0':
				tty_dev[0] = optarg;
				break;
			case '1':
				tty_dev[1] = optarg;
				break;
			case 'h':
			case '?':
				default:
				usages(argv[0]);
		}
	}

	cmd_buf = (char *)malloc(sizeof(char) * BUFSIZ); 
	msg = (char *)malloc(sizeof(char) * BUFSIZ); 

	for(i=0;i<2;i++) {
		if((fd[i] = open(tty_dev[i], O_RDWR | O_NONBLOCK)) < 0) {
			snprintf(msg, sizeof(char)*BUFSIZ, "can not open %s", tty_dev[i]);
			perror(msg);
			exit(0);
		}
	}

	init_signal();

	setbuf(stdin,NULL);
	setbuf(stdout,NULL);

	FD_ZERO(&mask);
	FD_SET(STDIN_FILENO,&mask);
	for(i=0;i<2;i++)
		FD_SET(fd[i],&mask);
	wd=fd[1]+2;

	for(i=0;i<2;i++) {
		tcgetattr(fd[i],&term_def[i]);
		bzero(&term[i], sizeof(term[i])); 

		term[i].c_cflag = CS8 | CREAD | CLOCAL | MDMBUF;
		term[i].c_iflag = IGNBRK | IGNPAR;
		term[i].c_oflag = 0;
		term[i].c_lflag = 0;

		term[i].c_cc[VTIME]    = 1;
		term[i].c_cc[VMIN]     = 0;

		cfsetispeed(&term[i],B_TS590);
		cfsetospeed(&term[i],B_TS590);

		tcflush(fd[i],TCIOFLUSH);
		tcsetattr(fd[i], TCSANOW, &term[i]);

		// turn on DTR
		iFlags[i] = TIOCM_DTR | TIOCM_DSR;
		ioctl(fd[i], TIOCMBIS, &iFlags[i]);
	}

	for(i=0;i<2;i++) {
		write(fd[i],";;",2);
		usleep(100000);
		read(fd[i], cmd_buf, BUFSIZ);
		buf = write_cmd(fd[i],"ID;");
		for(j=0;j<2;j++) {
			if(strncmp(buf, ts590_ver[j].id, 5) == 0) {
				g_series = j;
			}
		}
		if (g_series < 0) { 
			snprintf(msg, sizeof(char)*BUFSIZ, "unknown id %s", buf);
			perror(msg);
			proc_abort(SIGTERM);
		}
		buf = write_cmd(fd[i],"FV;");
		if(strncmp(buf, ts590_ver[g_series].fw, 6) != 0) {
			snprintf(msg, sizeof(char)*BUFSIZ, "Firmware version %s, required %s for %s",
				buf, ts590_ver[g_series].fw, ts590_ver[g_series].model);
			perror(msg);
			proc_abort(SIGTERM);
		}
	}
	write(fd[0],"AI2;",4);

	while (1) {
		timeout.tv_sec=10;
		timeout.tv_usec=0;
		rd=mask;
		switch(select(wd,&rd,NULL,NULL,&timeout)){
			case -1:
				if(errno!=EINTR){
					perror("select()");
					proc_abort(SIGTERM);
				}
				break;
			case  0:
				break;
			default:
				if(FD_ISSET(fd[0],&rd)){
					buf = read_cmd(fd[0]);
					if(*buf == ';') continue;
					n = 1;
					for(i=0;i<NUM_DENIED_CMDS;i++) {
						if(strncmp(denied_cmds[i], buf, 2) == 0) {
							n = 0;
							break;
						}
					}
					if(n && g_series) {
						for(i=0;i<NUM_G_ONLY_CMDS;i++) {
							if(strncmp(g_only_cmds[i], buf, 2) == 0) {
								n = 0;
								break;
							}
						}
					}
					if(n) {
						printf("%s\n", buf); 
						write(fd[1], buf, strlen(buf));
					}
				}
				break;
		}
	}
}
