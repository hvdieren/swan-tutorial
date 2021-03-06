#include "swan/wf_interface.h"

#define QSIZE   1023
#define RESERVE QSIZE

void generate( obj::pushdep<float> out, size_t n ) {
    for( size_t i=0; i < n; ) {
	size_t ni = std::min(size_t(RESERVE),n-i);
	obj::write_slice<obj::queue_metadata, float> ws
	    = out.get_write_slice( ni );
	for( int j=0; j < ni; ++j ) {
	    float f = i+j+1;
	    ws.push( f );
	}
	i += ni;
	ws.commit();
    }
}

void delay( obj::popdep<float> in, obj::pushdep<float> out, size_t n ) {
    float prev = 0;
    while( !in.empty() ) {
	obj::read_slice<obj::queue_metadata, float> rs
	    = in.get_slice_upto( RESERVE, 0 );
	obj::write_slice<obj::queue_metadata, float> ws
	    = out.get_write_slice( 2*std::min(size_t(RESERVE),rs.get_npops()) );
	for( size_t i=0; i < rs.get_npops(); ++i ) {
	    float f = rs.pop();
	    ws.push( f );
	    ws.push( prev );
	    prev = f;
	}
	rs.commit();
	ws.commit();
    }
}

void filter( obj::popdep<float> in, obj::pushdep<float> out, float k, size_t n ) {
    while( !in.empty() ) {
	obj::read_slice<obj::queue_metadata, float> rs
	    = in.get_slice_upto( RESERVE, 0 );
	obj::write_slice<obj::queue_metadata, float> ws
	    = out.get_write_slice( std::min(size_t(RESERVE),rs.get_npops()) );
	for( size_t i=0; i < rs.get_npops()/2; ++i ) {
	    float f0 = rs.pop();
	    float f1 = rs.pop();
	    float ei = f0 - k * f1;
	    float ebari = f1 - k * f0;
	    ws.push( ei );
	    ws.push( ebari );
	}
	rs.commit();
	ws.commit();
    }
}

void delay_filter( obj::popdep<float> in, obj::pushdep<float> out, float k, size_t n ) {
    float prev = 0;
    while( !in.empty() ) {
	obj::read_slice<obj::queue_metadata, float> rs
	    = in.get_slice_upto( RESERVE, 0 );
	obj::write_slice<obj::queue_metadata, float> ws
	    = out.get_write_slice( 2*std::min(size_t(RESERVE),rs.get_npops()) );
	for( size_t i=0; i < rs.get_npops(); ++i ) {
	    float f = rs.pop();
	    float f0 = f;
	    float f1 = prev;
	    prev = f;
	    float ei = f0 - k * f1;
	    float ebari = f1 - k * f0;
	    ws.push( ei );
	    ws.push( ebari );
	}
	rs.commit();
	ws.commit();
    }
}


void final( obj::popdep<float> in, size_t n ) {
    float running = 0;
    while( !in.empty() ) {
	obj::read_slice<obj::queue_metadata, float> rs
	    = in.get_slice_upto( RESERVE, 0 );
	for( size_t i=0; i < rs.get_npops(); ++i ) {
	    float v = rs.pop();
	    // printf( "%f\n", v );
	    running += v;
	}
	rs.commit();
    }
}

void work( size_t n ) {
    obj::hyperqueue<float> * q1[11];
    obj::hyperqueue<float> * q2[11];

    for( int i=2; i <= 10; ++i ) {
	q1[i] = new obj::hyperqueue<float>( QSIZE, 0 );
	q2[i] = new obj::hyperqueue<float>( QSIZE, 0 );
    }

    spawn( generate, (obj::pushdep<float>)*q1[2], n );
    for( int i=2; i < 10; ++i ) {
	spawn( delay_filter, (obj::popdep<float>)*q1[i],
	       (obj::pushdep<float>)*q1[i+1], (float)i, n );
/*
	spawn( delay, (obj::popdep<float>)*q1[i],
	       (obj::pushdep<float>)*q2[i], n );
	spawn( filter, (obj::popdep<float>)*q2[i],
	       (obj::pushdep<float>)*q1[i+1], (float)i, n );
*/
    }
    spawn( final, (obj::popdep<float>)*q1[10], n );

    ssync();
}

int main( int argc, char * argv[] ) {
    size_t n = 10;
    if( argc > 1 )
	n = atoi( argv[1] );

    run( work, n );

    return 0;
}

