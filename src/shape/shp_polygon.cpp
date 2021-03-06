//#include "stdafx.h"
# include "shp_polygon.h"

shp_polygon::shp_polygon()
{	shapetype = API_SHP_POLYGON;
}

shp_polygon::shp_polygon( const shp_polygon & p )
	:shape(p)
{	
}

int shp_polygon::recordbyteLength()
{	
	int numberbytes = shape::recordbyteLength();
	numberbytes += sizeof(int);
	numberbytes += sizeof(double)*4;
	numberbytes += sizeof(int);
	numberbytes += sizeof(int);
	if( parts.size() > 0 )
		numberbytes += sizeof(int)*parts.size();
	else
		numberbytes += sizeof(int);
	numberbytes += sizeof(double)*2*allPoints.size();

	return numberbytes;
}

bool shp_polygon::read( FILE * f, int & bytesRead )	
{	
	shape::read( f, bytesRead );

	int type;
	fread(&type,sizeof(int),1,f);

	bytesRead += sizeof(int);
	if(type != API_SHP_POLYGON)
	{	std::cout << "Shape type does not match in record."<<std::endl;
		return false;		
	}
	
	//input Bounding Box
	//Assign Class Variables topLeft && bottomRight
	double mybounds[4];
	fread(mybounds,sizeof(double),4,f);
	bytesRead += sizeof(double)*4;
	topLeft = api_point( mybounds[0], mybounds[3] );
	bottomRight = api_point( mybounds[2], mybounds[1] );

	int numParts = 0;
	int numPoints = 0;
	//input Number of Parts and Number of Points
	fread(&numParts,sizeof(int),1,f);
	bytesRead += sizeof(int);
	fread(&numPoints,sizeof(int),1,f);
	bytesRead += sizeof(int);
	
	//Allocate space for numparts
	int * p_parts = new int[numParts];
	fread(p_parts,sizeof(int),numParts,f);
	bytesRead += sizeof(int)*numParts;
	//Input parts
	for( int j = 0; j < numParts; j++ )
		parts.push_back( p_parts[j] );	
	
	//Input points
	double x,y;
	for(int i=0;i<numPoints;i++)
	{
		fread(&x,sizeof(double),1,f);
		bytesRead += sizeof(double);
		fread(&y,sizeof(double),1,f);
		bytesRead += sizeof(double);
		api_point p(x,y);
		insertPoint( p, i );
	}

	delete p_parts;

	return true;
}

shp_polygon shp_polygon::operator=( const shp_polygon & sp )	
{	
	parts.clear();
	for( int i = 0; i < sp.parts.size(); i++ )
		parts.push_back( sp.parts[i] );

	return *this;
}

api_point shp_polygon::shapeMiddle( api_point topLeftBound, api_point bottomRightBound )
{	api_point p;
	p.setX( ( topLeft.getX() + bottomRight.getX() ) / 2 );
	p.setY( ( topLeft.getY() + bottomRight.getY() ) / 2 );
	return p;
}

void shp_polygon::writeShape( FILE * out, int recordNumber )
{	writeRecordHeader( out, recordNumber );
	
	fwrite(&shapetype,sizeof(int),1, out);
	
	double shapeBounds[4];
	shapeBounds[0] = topLeft.getX();
	shapeBounds[1] = bottomRight.getY();
	shapeBounds[2] = bottomRight.getX();
	shapeBounds[3] = topLeft.getY();
	fwrite(shapeBounds,sizeof(double),4, out);
	
	int numParts = parts.size();
	if( numParts <= 0 )
		numParts = 1;
	fwrite(&numParts,sizeof(int),1, out);
	int numPoints = allPoints.size();
	fwrite(&numPoints,sizeof(int),1, out);
	
	if( parts.size() > 0 )
	{	for( int i = 0; i < parts.size(); i++ )
		{	int recordPart = parts[i];
			fwrite(&recordPart,sizeof(int), 1, out);		
		}
	}
	else
	{	int recordPart = 0;
		fwrite( &recordPart, sizeof(int), 1, out );
	}

	//Input points
	double x, y;	
	for( int j = 0; j < allPoints.size(); j++ )
	{	api_point p = allPoints[j];
		x = p.getX();
		y = p.getY();		
		fwrite(&x,sizeof(double),1, out);
		fwrite(&y,sizeof(double),1, out);
	}
}

bool shp_polygon::deletePoint( int position )
{	return shape::deletePoint( position );
}

int shp_polygon::insertPoint( api_point p, int position )
{	return shape::insertPoint( p, position );
}

bool shp_polygon::setPoint( api_point p, int position )
{	return shape::setPoint( p, position );
}

#ifdef SHAPE_OCX
		std::deque<CString> shp_polygon::getMembers()
		{	std::deque<CString> members;
			members.push_back( intToCString( shapetype ) );
			for( int i = 0; i < allPoints.size(); i++ )
			{	members.push_back( doubleToCString( allPoints[i].getX() ) );
				members.push_back( doubleToCString( allPoints[i].getY() ) );
			}
			return members;
		}

		bool shp_polygon::setMembers( std::deque<CString> members )
		{	try
			{	if( members.size() < 3 || ( members.size() - 1 )%2 != 0 )
					throw( Exception("Cannot create shp_polygon ... wrong number of parameters.") );
				if( members[0] != API_SHP_POLYGON )
					throw( Exception("  Cannot create shp_polygon.  Shape type invalid." ) );
				
				for( int i = 1; i < members.size(); i += 2 )
				{	api_point p;
					p.setX( CStringToDouble( members[i] ) );
					p.setY( CStringToDouble( members[i+1] ) );
					insertPoint( p, 0 );
				}
				return true;
			}
			catch( Exception e )
			{	return false;
			}
		}
#endif

