

/*
 * VISCA(tm) Camera Control Library
 * Copyright (C) 2002 Damien Douxchamps 
 *
 * Written by Damien Douxchamps <ddouxchamps@users.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "libvisca.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <syslog.h>


/* implemented in libvisca.c
 */
void _VISCA_append_byte(VISCAPacket_t * packet, unsigned char byte);
void _VISCA_init_packet(VISCAPacket_t * packet);
unsigned int _VISCA_get_reply(VISCAInterface_t * iface, VISCACamera_t * camera);
unsigned int _VISCA_send_packet_with_reply(VISCAInterface_t * iface, VISCACamera_t * camera, VISCAPacket_t * packet);
unsigned int _VISCA_send_packet_without_reply(VISCAInterface_t * iface, VISCACamera_t * camera, VISCAPacket_t * packet);



/* Implementation of the platform specific code. The following functions must
 * be implemented here:
 *
 * unsigned int _VISCA_write_packet_data(VISCAInterface_t *iface, VISCACamera_t *camera, VISCAPacket_t *packet);
 * unsigned int _VISCA_send_packet(VISCAInterface_t *iface, VISCACamera_t *camera, VISCAPacket_t *packet);
 * unsigned int _VISCA_get_packet(VISCAInterface_t *iface);
 * unsigned int VISCA_open_serial(VISCAInterface_t *iface, const char *device_name);
 * unsigned int VISCA_close_serial(VISCAInterface_t *iface);
 * 
 */
uint32_t _VISCA_write_packet_data(VISCAInterface_t * iface, VISCACamera_t * camera, VISCAPacket_t * packet)
{
    int err;
    int i = 0;
    char str[64]={0};
    char* strp = str;
    
    for(i=0;i<packet->length;i++)
    {
        sprintf(strp,"%02x ",packet->bytes[i]);
        strp+=3;
    }
    VISCA_LOGI("visca write packet data : %s",str);
    //err = sendto(iface->port_fd, packet->bytes, packet->length,0, (struct sockaddr *)&(iface->addr_serv), iface->addrlen);
	err = send(iface->port_fd,packet->bytes,packet->length,0);
    VISCA_LOGI("visca write packet data len: %d",err);
    if(err < packet->length)
        return VISCA_FAILURE;

    else
        return VISCA_SUCCESS;
}



uint32_t _VISCA_send_packet(VISCAInterface_t * iface, VISCACamera_t * camera, VISCAPacket_t * packet)
{
    // check data:
    if((iface->address > 7) || (camera->address > 7) || (iface->broadcast > 1))
    {
#if DEBUG
        fprintf(stderr, "(%s): Invalid header parameters\n", __FILE__);
        fprintf(stderr, " %d %d %d   \n", iface->address, camera->address, iface->broadcast);
#endif

        return VISCA_FAILURE;
    }

    // build header:
    packet->bytes[0] = 0x80;
    packet->bytes[0] |= (iface->address << 4);

    if(iface->broadcast > 0)
    {
        packet->bytes[0] |= (iface->broadcast << 3);
        packet->bytes[0] &= 0xF8;
    }
    else
        packet->bytes[0] |= camera->address;

    // append footer
    _VISCA_append_byte(packet, VISCA_TERMINATOR);

    return _VISCA_write_packet_data(iface, camera, packet);
}


uint32_t _VISCA_get_packet(VISCAInterface_t * iface)
{
/*    int pos = 0;
    int bytes_read;
    //bytes_read = recvfrom(iface->port_fd, iface->ibuf, VISCA_INPUT_BUFFER_SIZE, 0, (struct sockaddr *) &(iface->addr_serv), (socklen_t *) &len);
	bytes_read = recv(iface->port_fd,iface->ibuf,VISCA_INPUT_BUFFER_SIZE,0);
	if(bytes_read <= 0)
	{
		printf("------>visca get packet recv len: %d\n",bytes_read);
		return VISCA_FAILURE;
	}
	iface->bytes = bytes_read;
*/
    int pos=0;
    int bytes_read;
#if 0
    // wait for message
    ioctl(iface->port_fd, FIONREAD, &(iface->bytes));
    if(iface->bytes==0)
    {
	    usleep(50000);
	    ioctl(iface->port_fd, FIONREAD, &(iface->bytes));
    }
    if(iface->bytes==0)
    {
        VISCA_LOGE("------>recv packet failed");
        return VISCA_FAILURE;
    }
#endif
    ioctl(iface->port_fd, FIONREAD, &(iface->bytes));
    while (iface->bytes==0) 
    {
	    usleep(0);
	    ioctl(iface->port_fd, FIONREAD, &(iface->bytes));
    }
    // get octets one by one
    bytes_read=read(iface->port_fd, iface->ibuf, 1);
    while ((iface->ibuf[pos]!=VISCA_TERMINATOR)&&(pos<VISCA_INPUT_BUFFER_SIZE-1))
    {
	    pos++;
	    bytes_read=read(iface->port_fd, &iface->ibuf[pos], 1);
	    usleep(0);
    }
    iface->bytes=pos+1;
    
    int i = 0;
    char str[32]={0};
    char* strp = str;
    
    for(i=0;i<iface->bytes;i++)
    {
        sprintf(strp,"%02x ",iface->ibuf[i]);
        strp+=3;
    }
    VISCA_LOGI("visca get packet : %s",str);
    
    return VISCA_SUCCESS;
}



/***********************************/
/*       SYSTEM  FUNCTIONS         */
/***********************************/
uint32_t VISCA_open_serial(VISCAInterface_t * iface, const char * device_name)
{
    int fd;

    fd = open(device_name, O_RDWR | O_NDELAY | O_NOCTTY);

    if(fd == -1)
    {
#if DEBUG
        fprintf(stderr, "(%s): cannot open serial device %s\n", __FILE__, device_name);
#endif

        iface->port_fd = -1;
        return VISCA_FAILURE;
    }
    else
    {

    }

    iface->port_fd = fd;
    iface->address = 0;

    return VISCA_SUCCESS;
}


uint32_t VISCA_open_socket(VISCAInterface_t * iface, const char* addr,int port)
{
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);

    if(fd == -1)
    {
#if DEBUG
        fprintf(stderr, "(%s): cannot open serial device %s\n", __FILE__, device_name);
#endif

        iface->port_fd = -1;
        return VISCA_FAILURE;
    }
    else
    {
        struct sockaddr_in addr_serv, addr_client;
        memset(&addr_serv, 0, sizeof(addr_serv));
		memset(&addr_client, 0, sizeof(addr_client));
		
        //addr_client.sun_family = AF_UNIX;
		//strcpy(addr_client.sun_path,client);

		addr_serv.sin_family = AF_INET;
		addr_serv.sin_port = htons(port);
		addr_serv.sin_addr.s_addr=inet_addr(addr);
        
        iface->addr_serv = addr_serv;
		iface->addrlen = sizeof(iface->addr_serv);
        //unlink(addr_client.sun_path);
		//int size = offsetof(struct sockaddr_un, sun_path) + strlen(addr_client.sun_path); 
		//bind(fd, (struct sockaddr*)&addr_client, size);

		//addr_serv.sun_family=AF_UNIX;
		//strcpy(addr_serv.sun_path,serv);
        //size = offsetof(struct sockaddr_un, sun_path) + strlen(addr_serv.sun_path); 
	    if(connect(fd, (struct sockaddr *)&addr_serv, sizeof(struct sockaddr_in)) < 0)
		{  
            VISCA_LOGE("connect error");  
        }  

	    int flag = fcntl(fd, F_GETFL, 0);
        if (flag < 0) 
	    {
            VISCA_LOGE("fcntl F_GETFL fail");
            return;
        }
        if (fcntl(fd, F_SETFL, flag | O_NONBLOCK) < 0) 
		{
            VISCA_LOGE("fcntl F_SETFL fail");
        }
    }

    iface->port_fd = fd;
    iface->address = 0;

    return VISCA_SUCCESS;
}


uint32_t VISCA_unread_bytes(VISCAInterface_t * iface, unsigned char * buffer, uint32_t * buffer_size)
{
    uint32_t bytes = 0;

    *buffer_size = 0;

    ioctl(iface->port_fd, FIONREAD, &bytes);

    if(bytes > 0)
    {
        bytes = (bytes > *buffer_size) ? *buffer_size: bytes;
        read(iface->port_fd, &buffer, bytes);
        *buffer_size = bytes;
        return VISCA_FAILURE;
    }

    return VISCA_SUCCESS;
}


uint32_t VISCA_close_serial(VISCAInterface_t * iface)
{
    if(iface->port_fd != -1)
    {
        close(iface->port_fd);
        iface->port_fd = -1;
        return VISCA_SUCCESS;
    }
    else
        return VISCA_FAILURE;
}


uint32_t VISCA_usleep(uint32_t useconds)
{
    return (uint32_t)usleep(useconds);
}

uint32_t VISCA_set_osd_switch(VISCAInterface_t * iface, VISCACamera_t * camera)
{
    VISCAPacket_t packet;

    _VISCA_init_packet(&packet);
    _VISCA_append_byte(&packet, VISCA_COMMAND);
    _VISCA_append_byte(&packet, VISCA_CATEGORY_OSD1);
    _VISCA_append_byte(&packet, VISCA_OSD_STATUS);
    _VISCA_append_byte(&packet, VISCA_OSD_STATUS_SWITCH);

    return _VISCA_send_packet_with_reply(iface, camera, &packet);
}



uint32_t VISCA_set_osd_on(VISCAInterface_t * iface, VISCACamera_t * camera)
{
    VISCAPacket_t packet;

    _VISCA_init_packet(&packet);
    _VISCA_append_byte(&packet, VISCA_COMMAND);
    _VISCA_append_byte(&packet, VISCA_CATEGORY_OSD1);
    _VISCA_append_byte(&packet, VISCA_OSD_STATUS);
    _VISCA_append_byte(&packet, VISCA_OSD_STATUS_ON);

    return _VISCA_send_packet_with_reply(iface, camera, &packet);
}


uint32_t VISCA_set_osd_off(VISCAInterface_t * iface, VISCACamera_t * camera)
{
    VISCAPacket_t packet;

    _VISCA_init_packet(&packet);
    _VISCA_append_byte(&packet, VISCA_COMMAND);
    _VISCA_append_byte(&packet, VISCA_CATEGORY_OSD1);
    _VISCA_append_byte(&packet, VISCA_OSD_STATUS);
    _VISCA_append_byte(&packet, VISCA_OSD_STATUS_OFF);

    return _VISCA_send_packet_with_reply(iface, camera, &packet);
}


uint32_t VISCA_set_osd_ok(VISCAInterface_t * iface, VISCACamera_t * camera)
{
    VISCAPacket_t packet;

    _VISCA_init_packet(&packet);
    _VISCA_append_byte(&packet, VISCA_COMMAND);
    _VISCA_append_byte(&packet, VISCA_CATEGORY_OSD3);
    _VISCA_append_byte(&packet, VISCA_OSD_OK1);
    _VISCA_append_byte(&packet, VISCA_OSD_OK2);
    _VISCA_append_byte(&packet, VISCA_OSD_OK3);
    _VISCA_append_byte(&packet, VISCA_OSD_OK4);

    return _VISCA_send_packet_with_reply(iface, camera, &packet);
}


uint32_t VISCA_set_osd_back(VISCAInterface_t * iface, VISCACamera_t * camera)
{
    VISCAPacket_t packet;

    _VISCA_init_packet(&packet);
    _VISCA_append_byte(&packet, VISCA_COMMAND);
    _VISCA_append_byte(&packet, VISCA_CATEGORY_OSD2);
    _VISCA_append_byte(&packet, VISCA_OSD_BACK1);
    _VISCA_append_byte(&packet, VISCA_OSD_BACK2);

    return _VISCA_send_packet_with_reply(iface, camera, &packet);
}

uint32_t VISCA_get_osd_status(VISCAInterface_t *iface, VISCACamera_t *camera, uint8_t *status)
{
  VISCAPacket_t packet;
  uint32_t err;

  _VISCA_init_packet(&packet);
  _VISCA_append_byte(&packet, VISCA_INQUIRY);
  _VISCA_append_byte(&packet, VISCA_CATEGORY_OSD1);
  _VISCA_append_byte(&packet, VISCA_OSD_STATUS);
  err=_VISCA_send_packet_with_reply(iface, camera, &packet);
  if (err!=VISCA_SUCCESS)
    return err;
  else
    {
      *status=iface->ibuf[2];
      return VISCA_SUCCESS;
    }
}




