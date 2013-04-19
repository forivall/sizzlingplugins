
/*========
        This file is part of SizzlingPlugins.

    Copyright (c) 2010-2013, Jordan Cristiano.
    This file is subject to the terms and conditions 
    defined in the file 'LICENSE', which is part of this
    source code package.
    
*/

////////////////////////////////////////////////////////////////////////////////
// Filename: downloader.cpp
////////////////////////////////////////////////////////////////////////////////

#include "downloader.h"
#include "checksum_crc.h"
#include "utlbuffer.h"

#include "curlconnection.h"

// TODO: get rid of curl and use sockets @_@

static size_t rcvData(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	CUtlBuffer *pBuffer = static_cast<CUtlBuffer*>(userdata);
	pBuffer->Put( ptr, size*nmemb );
	//Msg( (char*)ptr ); // up to 989 characters each time
	return size * nmemb;
}

// should the downloader keep trying if it keeps failing to dl the file?
bool SizzDownloader::DownloadFile( const char *url, CUtlBuffer &buf )
{
	CCurlConnection connection;
	if (connection.Initialize())
	{
		connection.SetUrl(const_cast<char*>(url));
		connection.SetBodyWriteFunction(&rcvData);
		connection.SetBodyWriteUserdata(&buf);

	#ifndef NDEBUG
		connection.SetOption(CURLOPT_VERBOSE, 1L);
	#endif

		CURLcode ret = connection.Perform();
		connection.Close();

		if (ret != CURLE_OK)
		{
			/* we failed */ 
			Msg( "curl told us %d\n", ret );
			return false;
		}
		return true;
	}

	return false;
}

bool SizzDownloader::DownloadFileAndVerify( const char *url, unsigned int crc, CUtlBuffer &buf )
{
	// keeps trying until it gets it right
	// 5 tries max
	int failureCount = 0;	
	for (;;)
	{
		buf.Clear();
		if ( DownloadFile( url, buf ) && VerifyFile( crc, buf ) )
		{
			return true;
		}
		else
		{
			++failureCount;
			Msg( "failed to download file: %i time(s)\n", failureCount );
			if  ( failureCount == 5 )
			{
				Msg( "aborting download due to 5 successive failures\n" );
				return false;
			}
		}
	}
}

bool SizzDownloader::VerifyFile( const unsigned int actualCRC, CUtlBuffer const &buf )
{
	CRC32_t crc = CRC32_ProcessSingleBuffer( buf.PeekGet(), buf.GetBytesRemaining() );

	if ( crc == actualCRC )
	{	
		return true;
	}
	else
	{
		Msg( "file crc: %x, actual crc: %x\n", crc, actualCRC );
		return false;
	}
}
