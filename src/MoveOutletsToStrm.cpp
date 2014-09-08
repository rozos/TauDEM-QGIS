/*  MoveOutletsToStrm function to move outlets to a stream.
     
  David Tarboton, Teklu Tesfa, Dan Watson
  Utah State University  
  May 23, 2010 
  

  This function moves outlet point that are off a stream raster grid down D8 flow directions 
  until a stream raster grid is encountered.  Input is a flow direction grid, stream raster grid 
  and outlets shapefile.  Output is a new outlets shapefile where each point has been moved to 
  coincide with the stream raster grid if possible.  A field 'dist_moved' is added to the new 
  outlets shapefile to indicate the changes made to each point.  Points that are already on the 
  stream raster (src) grid are not moved and their 'dist_moved' field is assigned a value 0.  
  Points that are initially not on the stream raster grid are moved by sliding them along D8 
  flow directions until one of the following occurs:
  a.	A stream raster grid cell is encountered before traversing the max_dist number of grid cells.  
   The point is moved and 'dist_moved' field is assigned a value indicating how many grid cells the 
   point was moved.
  b.	More thanthe max_number of grid cells are traversed, or the traversal ends up going out of 
  the domain (encountering a no data D8 flow direction value).  The point is not moved and the 
  'dist_moved' field is assigned a value of -1.

*/

/*  Copyright (C) 2010  David Tarboton, Utah State University

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License 
version 2, 1991 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

A copy of the full GNU General Public License is included in file 
gpl.html. This is also available at:
http://www.gnu.org/copyleft/gpl.html
or from:
The Free Software Foundation, Inc., 59 Temple Place - Suite 330, 
Boston, MA  02111-1307, USA.

If you wish to use or incorporate this program (or parts of it) into 
other software that does not meet the GNU General Public License 
conditions contact the author to request permission.
David G. Tarboton  
Utah State University 
8200 Old Main Hill 
Logan, UT 84322-8200 
USA 
http://www.engineering.usu.edu/dtarb/ 
email:  dtarb@usu.edu 
*/

//  This software is distributed from http://hydrology.usu.edu/taudem/

// 1/25/14.  Modified to use shapelib by Chris George

#include <mpi.h>
#include <math.h>
#include <queue>
#include "commonLib.h"
#include "linearpart.h"
#include "createpart.h"
#include "tiffIO.h"
#include "shapelib/shapefil.h"
#include "MoveOutletsToStrm.h"
using namespace std;



int outletstosrc(char *pfile, char *srcfile, char *outletshapefile, char *movedoutletshapefile, int maxdist)
{

	MPI_Init(NULL,NULL);{
		int rank,size;
		MPI_Comm_rank(MCW,&rank);
		MPI_Comm_size(MCW,&size);
		if(rank==0)printf("MoveOutletsToStreams version %s\n",TDVERSION);

		double begin,end;
		//Begin timer
		begin = MPI_Wtime();
		int d1[9] = {-99,0,-1,-1,-1,0,1,1,1};
		int d2[9] = {-99,1,1,0,-1,-1,-1,0,1};

		//load the stream raster grid into a linear partition
		//Create tiff object, read and store header info
		//	MPI_Abort(MCW,5);
		tiffIO src(srcfile, SHORT_TYPE);
		long srcTotalX = src.getTotalX();
		long srcTotalY = src.getTotalY();
		double srcdx = src.getdx();
		double srcdy = src.getdy();
		if(rank==0)
		{
			float timeestimate=(2e-7*srcTotalX*srcTotalY/pow((double) size,0.65))/60+1;  // Time estimate in minutes
			fprintf(stderr,"This run may take on the order of %.0f minutes to complete.\n",timeestimate);
			fprintf(stderr,"This estimate is very approximate. \nRun time is highly uncertain as it depends on the complexity of the input data \nand speed and memory of the computer. This estimate is based on our testing on \na dual quad core Dell Xeon E5405 2.0GHz PC with 16GB RAM.\n");
			fflush(stderr);
		}


		//Create partition and read data
		tdpartition *srcData;
		srcData = CreateNewPartition(src.getDatatype(), srcTotalX, srcTotalY, srcdx, srcdy, src.getNodata());
		int srcnx = srcData->getnx();
		int srcny = srcData->getny();
		int srcxstart, srcystart;  // DGT Why are these declared as int if they are to be used as long
		srcData->localToGlobal(0, 0, srcxstart, srcystart);  //  DGT here no typecast - but 2 lines down there is typecast - why

		src.read((long)srcxstart, (long)srcystart, (long)srcny, (long)srcnx, srcData->getGridPointer());

		//load the d8 flow grid into a linear partition
		//Create tiff object, read and store header info
		tiffIO p(pfile, SHORT_TYPE);
		long pTotalX = p.getTotalX();
		long pTotalY = p.getTotalY();
		double pdx = p.getdx();
		double pdy = p.getdy();

		//Create partition and read data
		tdpartition *flowData;
		flowData = CreateNewPartition(p.getDatatype(), pTotalX, pTotalY, pdx, pdy, p.getNodata());
		int pnx = flowData->getnx();
		int pny = flowData->getny();
		int pxstart, pystart;
		flowData->localToGlobal(0, 0, pxstart, pystart);
		p.read(pxstart, pystart, pny, pnx, flowData->getGridPointer());

		if(!p.compareTiff(src)){
			printf("src and p files not the same size. Exiting \n");
			MPI_Abort(MCW,4);
		}


		//load the shapefile that contains the unmoved src points
		//copy the shapefile to a new shapefile called shpmoved on p0

		//  Code added to read shape file
		SHPHandle sh, shmoved;
		DBFHandle dbf, dbfmoved;
		double *xnode, *ynode;
		double *origxnode, *origynode;
		int nxy;
		long *dist_moved;  //*ismoved,  DGT decided ismoved is not needed
		int *part_has;  // variable to keep track of which partition has control of outlet
		int nfields;
		int i,j;
		int * indexMap;
		DBFFieldType * types;
		int dmIndex;

		if(rank==0){
			sh = SHPOpen(outletshapefile, "rb");
			char outletsdbf[MAXLN];
			nameadd(outletsdbf, outletshapefile, ".dbf");
			dbf = DBFOpen(outletsdbf, "rb");
			if ((sh != NULL) && (dbf != NULL)) {
				// Strategy is to create a new shapefile with identical properties and fields
				shmoved = SHPCreate(movedoutletshapefile, SHPT_POINT);
				char movedoutletsdbf[MAXLN];
				nameadd(movedoutletsdbf, movedoutletshapefile, ".dbf");
				dbfmoved = DBFCreate(movedoutletsdbf);
				nfields=DBFGetFieldCount(dbf);
				indexMap = new int[nfields];
				types = new DBFFieldType[nfields];
				char *fieldname = new char[12];
				for(i=0; i<nfields; i++) {
					int * pWidth = NULL;
					int width = 0;
					int * pPrecision = NULL;
					int precision = 0;
					DBFFieldType type = DBFGetFieldInfo(dbf, i, fieldname, pWidth, pPrecision);
					types[i] = type;
					if (pWidth == NULL) {
						if (type == FTInteger) width = 6;
						else width = 12;
					} else {
						width = *pWidth;
					}
					if (pPrecision == NULL) {
						if (type = FTDouble) precision = 1;
						else precision = 0;
					} else {
						precision = *pPrecision;
					}
					if (type != FTInvalid) {
						int j = DBFAddField(dbfmoved, fieldname, type, width, precision);
						indexMap[i] = j;
					} else {
						indexMap[i] = -1;
					}
				}
				delete [] fieldname;
				//  Insert additional field to record distance moved
				dmIndex = DBFAddField(dbfmoved, "Dist_moved", FTInteger, 6, 0);

				nxy = DBFGetRecordCount(dbf);
				//int p_size;
				//  Code below commented out on assumption of one point per shape
				//int countPts = 0;
				//
				//for( int i=0; i<size; i++) {
				//	shp = sh.getShape(i);
				//	countPts += shp->size();
				//}	
			} else {
				printf("\nError opening shapefile.\n\n");	
				nxy=0;
				//		MPI_Abort(MCW,5);
			}
		}

		MPI_Bcast(&nxy, 1, MPI_INT, 0, MCW);
		if(nxy==0)
		{  // Attempt to exit gracefully on all processors
			if(rank==0)
				printf("Unable to read any points from shapefile\n\n");

			MPI_Finalize();
		}

		xnode = new double[nxy];
		ynode = new double[nxy];
		origxnode = new double[nxy];
		origynode = new double[nxy];
		//	ismoved = new long[nxy];  // DGT decided not needed 
		dist_moved = new long[nxy];
		part_has = new int[nxy];  // Variable to keep track of which partition currently has control over this outlet
		int itresh=1;  // Thresholding to 1 done in source

		if(rank==0){
			for(i=0; i<nxy; i++) {
				SHPObject *shp = SHPReadObject(sh, i);
				xnode[i] = shp->padfX[0];
				ynode[i] = shp->padfY[0];
				origxnode[i]=xnode[i];
				origynode[i]=ynode[i];
				//  Initializing
				//			ismoved[i] = 0;
				dist_moved[i] = 0;
				part_has[i]=-1;  // initialize part_has to -1 for all points.  This will be set to rank later
				SHPDestroyObject(shp);
			}
		}

		MPI_Bcast(xnode, nxy, MPI_DOUBLE, 0, MCW);
		MPI_Bcast(ynode, nxy, MPI_DOUBLE, 0, MCW);
		//	MPI_Bcast(ismoved, nxy, MPI_LONG, 0, MCW);
		MPI_Bcast(dist_moved, nxy, MPI_LONG, 0, MCW);
		MPI_Bcast(part_has, nxy, MPI_INT, 0, MCW);

		// oooooooooooooooooooooo  begin processing

		int *outletsX, *outletsY;
		int tx,ty;
		short td;
		outletsX = new int[nxy];
		outletsY = new int[nxy];
		int done = 0;
		int localdone = 0;
		int localnodes;
		int totalnodes;
		int totaldone;
		short dirn;
		int nextx,nexty;

		//Convert geo coords to grid coords
		for( i=0; i<nxy; i++)
			p.geoToGlobalXY(xnode[i], ynode[i], outletsX[i], outletsY[i]);

		while(!done){
			for( i=0; i<nxy; i++){
				flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
				if(flowData->isInPartition(tx,ty))part_has[i]=rank;  // grab control if in partition
				else part_has[i]=-1;  // some other partition has control
				if(flowData->isInPartition(tx,ty) &&dist_moved[i]>=0){
					td=srcData->getData(tx,ty,td);
					//	td = (char) td;  //DGT Why do we have to do this CHAR.  It seems like it was declared short so should all work
					if(srcData->isNodata(tx,ty) || td<itresh){  //  If not on stream.  The rule is any value >= itresh is a stream, less or no data is not stream
						//move the outlet
						dirn = flowData->getData(tx,ty,dirn);
						//dirn = (char) dirn;
						//  DGT added dist_moved condition below which is: 
						//  More than the max_number of grid cells are traversed, or the traversal ends up going out of the 
						//  domain (encountering a no data D8 flow direction value).  
						//  The point is not moved and the 'dist_moved' field is assigned a value of -1.
						if(dirn>=1 && dirn<=8 && dist_moved[i] < maxdist){
							nextx=outletsX[i]+d2[dirn];
							nexty=outletsY[i]+d1[dirn];
							if(nextx>=0 && nexty>=0 && nextx< pTotalX && nexty< pTotalY){  // If is within global domain
								outletsX[i]=nextx;
								outletsY[i]=nexty;
								//ismoved[i]=1;
								dist_moved[i]++;
								//printf("Outlet: %d, x: %d, y: %d\n",i,nextx,nexty);
							}else{
								// moved off the map
								dist_moved[i]=-1;
							}
						}else{
							//flow data not a direction
							dist_moved[i]=-1;
						}
					} 
					//  No else needed because if on stream do nothing
				}
			}

			//share data with neighbors
			//this code is linear partition specific
			int * toutletsX = new int[nxy];
			int * toutletsY = new int[nxy];
			//	long * tismoved = new long[nxy];
			long * tdist_moved = new long[nxy];
			MPI_Status status;
			int *ptr;
			int place;
			int *buf;
			long *lbuf;
			int bsize=nxy*sizeof(int)+MPI_BSEND_OVERHEAD;  
			int lsize=nxy*sizeof(long)+MPI_BSEND_OVERHEAD;  
			buf = new int[bsize];
			lbuf = new long[lsize];

			int txn, tyn;

			int dest = rank-1;
			if (dest<0)dest+=size;  
			if(size>1){
				MPI_Buffer_attach(buf,bsize);
				MPI_Bsend(outletsX, nxy, MPI_INT, dest, 0, MCW);
				MPI_Buffer_detach(&ptr,&place);
				MPI_Buffer_attach(buf,bsize);
				MPI_Bsend(outletsY, nxy, MPI_INT, dest, 1, MCW);
				MPI_Buffer_detach(&ptr,&place);
				MPI_Buffer_attach(lbuf,lsize);
				MPI_Bsend(dist_moved, nxy, MPI_LONG, dest, 3, MCW);
				MPI_Buffer_detach(&ptr,&place);
				MPI_Recv(toutletsX, nxy, MPI_INT, MPI_ANY_SOURCE, 0, MCW, &status);
				MPI_Recv(toutletsY, nxy, MPI_INT, MPI_ANY_SOURCE, 1, MCW, &status);
				MPI_Recv(tdist_moved, nxy, MPI_LONG, MPI_ANY_SOURCE, 3, MCW, &status);
			}
			for( i=0; i<nxy; i++){
				//if a node in the temp arrays belongs to us, and we don't already have it, get it
				flowData->globalToLocal(toutletsX[i], toutletsY[i], txn, tyn);  //DGT with single partition this is being passed uninitialized information
				flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
				if(flowData->isInPartition(txn,tyn) && !(part_has[i]==rank)){  // if an outlet not in partition is now in partition - grab it
					outletsX[i]=toutletsX[i];
					outletsY[i]=toutletsY[i];
					part_has[i]=rank;
					dist_moved[i]=tdist_moved[i];
				}
			}

			dest = rank+1;
			if (dest>=size)dest-=size;  //DGT Why is this here.  
			if(size>1){
				MPI_Buffer_attach(buf,bsize);
				MPI_Bsend(outletsX, nxy, MPI_INT, dest, 4, MCW);
				MPI_Buffer_detach(&ptr,&place);
				MPI_Buffer_attach(buf,bsize);
				MPI_Bsend(outletsY, nxy, MPI_INT, dest, 5, MCW);
				MPI_Buffer_detach(&ptr,&place);
				MPI_Buffer_attach(lbuf,lsize);
				MPI_Bsend(dist_moved, nxy, MPI_LONG, dest, 7, MCW);
				MPI_Buffer_detach(&ptr,&place);
				MPI_Recv(toutletsX, nxy, MPI_INT, MPI_ANY_SOURCE, 4, MCW, &status);
				MPI_Recv(toutletsY, nxy, MPI_INT, MPI_ANY_SOURCE, 5, MCW, &status);
				MPI_Recv(tdist_moved, nxy, MPI_LONG, MPI_ANY_SOURCE, 7, MCW, &status);
			}

			for( i=0; i<nxy; i++){
				//if a node in the temp arrays belongs to us, and we don't already have it, get it
				flowData->globalToLocal(toutletsX[i], toutletsY[i], txn, tyn);
				flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
				if(flowData->isInPartition(txn,tyn) && !(part_has[i]==rank)){
					outletsX[i]=toutletsX[i];
					outletsY[i]=toutletsY[i];
					dist_moved[i]=tdist_moved[i];
				}

			}
			delete [] toutletsX;
			delete [] toutletsY;
			delete [] tdist_moved;

			// each process figures out how many nodes it currently has and how many are done
			localnodes = 0;
			localdone = 0;
			for( i=0; i<nxy; i++){
				flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
				if(flowData->isInPartition(tx,ty)){
					localnodes++;
					td=srcData->getData(tx,ty,td);
					//td = (char) td; 
					if(td>=itresh && ! srcData->isNodata(tx,ty))localdone++;
					else if(dist_moved[i]<0)localdone++;  // DGT added this as a termination condition
				}
			}

			//total up all the nodes and all the finished nodes
			MPI_Reduce(&localnodes, &totalnodes, 1, MPI_INT, MPI_SUM, 0, MCW);
			MPI_Reduce(&localdone, &totaldone, 1, MPI_INT, MPI_SUM, 0, MCW);
			//if(!rank)printf("dist = %d\tfinished %d out of %d nodes.\n",dist, totaldone,totalnodes);

			//if all the nodes are done or we've moved them all maxdist, terminate the loop
			if(rank==0){
				if(totaldone==totalnodes){  
					done=1;
				}

			}
			MPI_Bcast(&done,1,MPI_INT,0,MCW);
		}

		// look for unresolved outlets, set dist_moved to -1
		for( i=0; i<nxy; i++){
			flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
			if(flowData->isInPartition(tx,ty)){
				td=srcData->getData(tx,ty,td);
				if(td<itresh && dist_moved[i]==maxdist){
					dist_moved[i]=-1;
					//				ismoved[i]=0;
				}
			}
		}

		// oooooooooooooooooooooo  end  processing

		// write the shapefile that contains the moved src points


		double *tempxnode, *tempynode;
		long  *tempdist_moved;  //  *tempismoved,
		tempxnode = new double[nxy];
		tempynode = new double[nxy];
		//	tempismoved = new long[nxy];
		tempdist_moved = new long[nxy];

		for(i=0;i<nxy;++i){
			tempxnode[i]=0;
			tempynode[i]=0;
			//		tempismoved[i]=0;
			tempdist_moved[i]=0;
		}

		//if(!rank)printf("setting up temp arrays...",dist, totaldone,totalnodes);
		// set temp arrays with local data in each process
		// each local array should have it's local data in the array at the proper place
		// zeros elsewhere
		for( i=0; i<nxy; i++){
			p.globalXYToGeo(outletsX[i], outletsY[i], xnode[i], ynode[i]);
			flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
			if(flowData->isInPartition(tx,ty)){
				tempxnode[i]=xnode[i];
				tempynode[i]=ynode[i];
				//			tempismoved[i]=ismoved[i];
				tempdist_moved[i]=dist_moved[i];
				/*
				printf("p%d:\t",rank);
				printf("x:%d\t",outletsX[i]);
				printf("y:%d\t",outletsY[i]);
				printf("strm:%d\t",td);
				printf("dir:%d\t",tf);
				printf("distmvd:%d\t",dist_moved[i]);
				printf("ismoved:%d\t",ismoved[i]);
				printf("\n");
				*/
			}
		}
		if(!rank){
			for( i=0; i<nxy; i++){
				// if the outlet is outside the DEM, p0 puts it back in the temp array
				if(outletsX[i]<0||outletsX[i]>=pTotalX||outletsY[i]<0||outletsY[i]>=pTotalY){
					tempxnode[i]=origxnode[i];
					tempynode[i]=origynode[i];
					//			tempismoved[i]=0;
					tempdist_moved[i]=-1;
				} 
				// if dist_moved == 0, use original points
			}
		}
		//if(!rank)printf("done.\n",dist, totaldone,totalnodes);


		//if(!rank)printf("reducing data...",dist, totaldone,totalnodes);
		MPI_Reduce(tempxnode, xnode, nxy, MPI_DOUBLE, MPI_SUM, 0, MCW);
		MPI_Reduce(tempynode, ynode, nxy, MPI_DOUBLE, MPI_SUM, 0, MCW);
		//	MPI_Reduce(tempismoved, ismoved, nxy, MPI_LONG, MPI_SUM, 0, MCW);
		MPI_Reduce(tempdist_moved, dist_moved, nxy, MPI_LONG, MPI_SUM, 0, MCW);
		//if(!rank)printf("done\n.",dist, totaldone,totalnodes);

		if(!rank){
			for( i=0; i<nxy; i++){
				// if the distance moved is 0, use original x y
				if(dist_moved[i]<=0){  // DGT changed condition to dist_moved <=0 because original values kept whenever not moved for whatever reason
					xnode[i]=origxnode[i];
					ynode[i]=origynode[i];
				} 
			}
		}

		delete [] tempxnode;
		delete [] tempynode;
		//	delete [] tempismoved;
		delete [] tempdist_moved;

		//if(!rank)printf("inserting shapes...",dist, totaldone,totalnodes);
		//if(rank==0)printf("--\n");
		if(rank==0){
			for(i=0;i<nxy;++i){
				double x = xnode[i];  // DGT says does not need +pdx/2.0;
				double y = ynode[i];  // DGT +pdy/2.0;
				SHPObject *shpmoved = SHPCreateSimpleObject(SHPT_POINT, 1, &x, &y, NULL);
				//if(rank==0)printf("x: %g \ty: %g\n",xnode[i],ynode[i]);
				//if(rank==0)printf("x: %g \ty: %g\tdist: %d\n",shpmoved->padfX[0],shpmoved->padfY[0],dist_moved[i]);
				int shapeIndx = SHPWriteObject(shmoved, -1, shpmoved);
				SHPDestroyObject(shpmoved);
				int res;
				for(j=0;j<nfields;++j){
					DBFFieldType type = types[j];
					int fieldIndx = indexMap[j];
					if ((type != FTInvalid) && (fieldIndx >= 0)) {
						if (type = FTInteger) {
							int val = DBFReadIntegerAttribute(dbf, i, j);
							res = DBFWriteIntegerAttribute(dbfmoved, shapeIndx, fieldIndx, val);
						} 
						else if (type == FTDouble) {
							double val = DBFReadDoubleAttribute(dbf, i, j);
							res = DBFWriteDoubleAttribute(dbfmoved, shapeIndx, fieldIndx, val);
						}
						else if (type == FTString) {
							const char * val = DBFReadStringAttribute(dbf, i, j);
							res = DBFWriteStringAttribute(dbfmoved, shapeIndx, fieldIndx, val);
						}
						else { // FTLogical:
							const char * val = DBFReadLogicalAttribute(dbf, i, j);
							res = DBFWriteLogicalAttribute(dbfmoved, shapeIndx, fieldIndx, val[0]);
						}
					}
					// CWG should check res is not zero
				}
				//  Add distance moved value
				res = DBFWriteIntegerAttribute(dbfmoved, i, dmIndex, (int)dist_moved[i]);
				// CWG should check res is not zero
			}
			//if(!rank)printf("closing file...",dist, totaldone,totalnodes);
			delete [] indexMap;
			delete [] types;
			SHPClose(sh);
			DBFClose(dbf);
			SHPClose(shmoved);
			DBFClose(dbfmoved);
		}
		//if(!rank)printf("done\n.",dist, totaldone,totalnodes);


		delete [] xnode;
		delete [] ynode;
		//	delete [] ismoved;
		delete [] dist_moved;
		delete [] outletsX;
		delete [] outletsY;
		delete [] part_has;
		end = MPI_Wtime();
		double total,temp;
		total = end-begin;
		MPI_Allreduce (&total, &temp, 1, MPI_DOUBLE, MPI_SUM, MCW);
		total = temp/size;


		if( rank == 0) 
			printf("Total time: %f\n",total);

	}MPI_Finalize();


	return 0;
}


