#include <iostream>
#include <mpi.h>
#include <string.h>
#include <atomic>

#include <ghex/transport_layer/ucx/threads.hpp>
#include <ghex/common/timer.hpp>
#include "utils.hpp"

int main(int argc, char *argv[])
{
    int rank, size, peer_rank;
    int niter, buff_size;
    int inflight;

    gridtools::ghex::timer timer;
    long bytes = 0;

    niter = atoi(argv[1]);
    buff_size = atoi(argv[2]);
    inflight = atoi(argv[3]);
    
    int mode;
#ifdef THREAD_MODE_MULTIPLE
    MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &mode);
    if(mode != MPI_THREAD_MULTIPLE){
	std::cerr << "MPI_THREAD_MULTIPLE not supported by MPI, aborting\n";
	std::terminate();
    }
#else
    MPI_Init_thread(NULL, NULL, MPI_THREAD_SINGLE, &mode);
#endif

    THREAD_PARALLEL_BEG() {

	int thrid, nthr;
	unsigned char **buffers = new unsigned char *[inflight];
	MPI_Request *req = new MPI_Request[inflight];
	MPI_Comm mpi_comm;

	THREAD_MASTER() {
	    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	    MPI_Comm_size(MPI_COMM_WORLD, &size);
	    peer_rank = (rank+1)%2;
	    if(rank==0)	std::cout << "\n\nrunning test " << __FILE__ << "\n\n";
	}

	thrid = GET_THREAD_NUM();
	nthr = GET_NUM_THREADS();

	/* duplicate the communicator - all threads in order */
	for(int tid=0; tid<nthr; tid++){
	    if(thrid==tid) {
		MPI_Comm_dup(MPI_COMM_WORLD, &mpi_comm);
	    }
	    THREAD_BARRIER();
	}
	
	for(int j=0; j<inflight; j++){
	    req[j] = MPI_REQUEST_NULL;
	    MPI_Alloc_mem(buff_size, MPI_INFO_NULL, &buffers[j]);	
	    memset(buffers[j], 1, buff_size);
	}

	THREAD_MASTER(){
	    MPI_Barrier(mpi_comm);
	}
	THREAD_BARRIER();

	THREAD_MASTER() {
	    if(rank == 1) {
		std::cout << "number of threads: " << nthr << ", multi-threaded: " << THREAD_IS_MT << "\n";
		timer.tic();
		bytes = (double)niter*size*buff_size;
	    }
	}

	int i = 0, dbg = 0;
	int ncomm;
	while(i<niter){

	    /* submit inflight async requests */
	    for(int j=0; j<inflight; j++){
		if(rank==0 && thrid==0 && dbg>=(niter/10)) {
		    std::cout << i << " iters\n";
		    dbg=0;
		}
		if(rank==0)
		    MPI_Isend(buffers[j], buff_size, MPI_BYTE, peer_rank, thrid*inflight+j, mpi_comm, &req[j]);
		else
		    MPI_Irecv(buffers[j], buff_size, MPI_BYTE, peer_rank, thrid*inflight+j, mpi_comm, &req[j]);
		ncomm++;
		dbg +=nthr; 
		i+=nthr;
	    }

	    /* wait for all to complete */
	    MPI_Waitall(inflight, req, MPI_STATUS_IGNORE);
	}

	MPI_Barrier(mpi_comm);
	THREAD_MASTER(){
	    MPI_Barrier(mpi_comm);
	}
	THREAD_BARRIER();

	std::cout << rank << ":" << thrid << " ncomm " << ncomm << "\n";
    }

    if(rank == 1) timer.vtoc(bytes);

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
}
