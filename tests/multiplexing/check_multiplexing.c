/* check_multiplexing.c						*/
/* by Vince Weaver   vincent.weaver _at_ maine.edu		*/

/* Prior to 2.6.34 perf_event multiplexing was unreliable	*/
/* The values were not updated properly.			*/
/* This was probably fixed by					*/
/* 45e16a6834b6af098702e5ea6c9a40de42ff77d8			*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <errno.h>

#include <sys/mman.h>

#include <sys/ioctl.h>
#include <asm/unistd.h>

#include "perf_event.h"
#include "test_utils.h"
#include "perf_helpers.h"
#include "instructions_testcode.h"

#define NUM_EVENTS 10

int fd[NUM_EVENTS];
long long events[NUM_EVENTS]={
	PERF_COUNT_HW_CPU_CYCLES,
	PERF_COUNT_HW_INSTRUCTIONS,
	PERF_COUNT_HW_CPU_CYCLES,
	PERF_COUNT_HW_INSTRUCTIONS,
	PERF_COUNT_HW_CPU_CYCLES,
	PERF_COUNT_HW_INSTRUCTIONS,
	PERF_COUNT_HW_CPU_CYCLES,
	PERF_COUNT_HW_INSTRUCTIONS,
	PERF_COUNT_HW_CPU_CYCLES,
	PERF_COUNT_HW_INSTRUCTIONS,
};

long long base_results[NUM_EVENTS][3];
long long mpx_results[NUM_EVENTS][5];
long long scaled_results[NUM_EVENTS];
long long scale;
double error[NUM_EVENTS];
int bad_error=0;

int test_routine(void) {

	int i,result;

	for(i=0;i<500;i++) {
		result=instructions_million();
	}

	return result;
}

int main(int argc, char** argv) {

	int ret,quiet,i;

	struct perf_event_attr pe;

	char test_string[]="Testing if multiplexing works...";

	quiet=test_quiet();

	if (!quiet) {
		printf("Before 2.6.34 the values returned when multiplexing\n");
		printf("  were not reliable and could result in wrong results.\n");
	}

	/*************************************************/
	/* Collect results for each counter individually */
	/*************************************************/

	for(i=0;i<NUM_EVENTS;i++) {

		memset(&pe,0,sizeof(struct perf_event_attr));
		pe.type=PERF_TYPE_HARDWARE;
		pe.size=sizeof(struct perf_event_attr);
		pe.config=events[i];
		pe.disabled=1;
		pe.exclude_kernel=1;
		pe.exclude_hv=1;
		pe.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED |
			PERF_FORMAT_TOTAL_TIME_RUNNING;

		arch_adjust_domain(&pe,quiet);

		fd[i]=perf_event_open(&pe,0,-1,-1,0);
		if (fd[i]<0) {
			fprintf(stderr,"Failed adding mpx event %d %s\n",
				i,strerror(errno));
			test_fail(test_string);
			return -1;
		}
	}

	for(i=0;i<NUM_EVENTS;i++) {
		ret=ioctl(fd[i], PERF_EVENT_IOC_ENABLE,0);
		if (ret<0) {
			fprintf(stderr,"Error starting event %d\n",i);
		}

		test_routine();

		ret=ioctl(fd[i], PERF_EVENT_IOC_DISABLE,0);
		if (ret<0) {
			fprintf(stderr,"Error stopping event %d\n",i);
		}

		ret=read(fd[i],&base_results[i],3*sizeof(long long));
		if (ret<3*sizeof(long long)) {
			fprintf(stderr,"Event %d unexpected read size %d\n",i,ret);
			test_fail(test_string);
		}

		if (base_results[i][1]!=base_results[i][2]) {
			fprintf(stderr,"Event %d running %lld != %lld base\n",
				i,base_results[i][1],base_results[i][2]);
			test_fail(test_string);
		}

	}

	/* we have to close, because PERF_EVENT_IOC_RESET */
	/* does not clear multiplexing informatio.        */
	for(i=0;i<NUM_EVENTS;i++) {
		close(fd[i]);
	}

	/*************************************************/
	/* Collect multiplexed results                   */
	/*************************************************/

	for(i=0;i<NUM_EVENTS;i++) {

		memset(&pe,0,sizeof(struct perf_event_attr));
		pe.type=PERF_TYPE_HARDWARE;
		pe.size=sizeof(struct perf_event_attr);
		pe.config=events[i];
		pe.disabled=1;
		pe.exclude_kernel=1;
		pe.exclude_hv=1;
		pe.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED |
				PERF_FORMAT_TOTAL_TIME_RUNNING;

		arch_adjust_domain(&pe,quiet);

		fd[i]=perf_event_open(&pe,0,-1,-1,0);
		if (fd[i]<0) {
			fprintf(stderr,"Failed adding mpx event %d %s\n",
				i,strerror(errno));
			test_fail(test_string);
			return -1;
		}
	}

	for(i=0;i<NUM_EVENTS;i++) {
		ret=ioctl(fd[i], PERF_EVENT_IOC_ENABLE,0);
		if (ret<0) {
			fprintf(stderr,"Error starting event %d\n",i);
		}
	}


	test_routine();

	for(i=0;i<NUM_EVENTS;i++) {
		ret=ioctl(fd[i], PERF_EVENT_IOC_DISABLE,0);
		if (ret<0) {
			fprintf(stderr,"Error stopping event %d\n",i);
		}
	}


	for(i=0;i<NUM_EVENTS;i++) {
		ret=read(fd[i],&mpx_results[i],5*sizeof(long long));
		if (ret<3*sizeof(long long)) {
			fprintf(stderr,"Event %d unexpected read size %d\n",
				i,ret);
			test_fail(test_string);
		}

		scale = (mpx_results[i][1] * 100LL) / mpx_results[i][2];
		scale = scale * mpx_results[i][0];
		scale = scale / 100LL;
		scaled_results[i] = scale;
	}

	for(i=0;i<NUM_EVENTS;i++) {
		error[i]=((double)base_results[i][0] -
				(double)scaled_results[i])/
				(double)base_results[i][0];
		error[i]*=100.0;
		if ((error[i]>10.0) || (error[i]<-10.0)) {
			bad_error++;
		}

	}

	if (!quiet) {
		printf("Event\tTotalCount\tRawCount\tScale\tScaledCount\tError\n");
		for(i=0;i<NUM_EVENTS;i++) {

			printf("%d\t%lld\t%lld\t%.2f\t%lld\t%.2f%%\n",i,
				base_results[i][0],
				mpx_results[i][0],
				(double)mpx_results[i][1]/(double)mpx_results[i][2],
				scaled_results[i],
				error[i]);

		}
	}

	if (bad_error>0) {
		fprintf(stderr,"Error on %d events is too high.\n",bad_error);
		test_fail(test_string);
	}

	test_pass(test_string);

	return 0;
}

