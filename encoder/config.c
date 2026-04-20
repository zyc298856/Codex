#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "main.h"
#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "cJSON.h"


int own_system(const char * cmd)
{
	FILE * fp;
	int res;
	char buf[1024];
	if ((fp = popen(cmd, "r") ) == NULL)
	{
		perror("popen");
		printf("popen error: %s/n", strerror(errno));
		return -1;
	}
	else
	{
		while(fgets(buf, sizeof(buf), fp))
		{
			printf("%s", buf);
		}

		if ( (res = pclose(fp)) == -1)
		{
			printf("close file pointer fp error!\n");
			return res;
		}
		else if (res == 0)
		{
			return res;
		}
		else
		{
			//printf("popen res is :%d\n", res);
			return res;
		}
	}
}

#define SAVE_NODE(nodes, value, key) \
		subnode = xmlNewNode(NULL, BAD_CAST key);\
		xmlAddChild(nodes, subnode);\
		sprintf(tmp, "%d", value);\
		xmlAddChild(subnode, xmlNewText(BAD_CAST tmp));

#define SAVE_NODED(nodes, value, key) \
		subnode = xmlNewNode(NULL, BAD_CAST key);\
		xmlAddChild(nodes, subnode);\
		sprintf(tmp, "%f", value);\
		xmlAddChild(subnode, xmlNewText(BAD_CAST tmp));

#define SAVE_NODES(nodes, value, key) \
		subnode = xmlNewNode(NULL, BAD_CAST key);\
		xmlAddChild(nodes, subnode);\
		xmlAddChild(subnode, xmlNewText(BAD_CAST (value)));

void config_WriteConfigFile( encoder_t *p_encoder )
{
	char tmp[256];
	char psz_filename[256];
	program_t*p_program = &p_encoder->m_program;

	strcpy(psz_filename, p_program->psz_dir);
	strcat(psz_filename, "config/encoder.xml");

    xmlDocPtr doc = xmlNewDoc(BAD_CAST"1.0");
	xmlNodePtr root_node = xmlNewNode(NULL, BAD_CAST"root");
	xmlDocSetRootElement(doc, root_node);

	xmlNodePtr node = xmlNewNode(NULL, BAD_CAST"system");

	xmlAddChild(root_node, node);

	xmlNodePtr subnode;

	SAVE_NODE(node, p_program->i_video_bitrate, "bitrate")
	SAVE_NODED(node, p_program->f_prob, "prob")
	SAVE_NODES(node, " '>' prob, display", "prob_comments")


	SAVE_NODE(node, p_program->b_compare, "compare")
	SAVE_NODES(node, "0:no,1:compare", "compare_comments")

	SAVE_NODE(node, p_program->i_input_type, "input_type")
	SAVE_NODES(node, "0:none, 1:camera, 2:avi file", "input_type_comments")

	SAVE_NODE(node, p_program->i_device, "camera")
	SAVE_NODES(node, "0:USB, 1:yuv file", "camera_comments")

	SAVE_NODES(node, p_program->psz_iurl, "input_url")

	int nRel = xmlSaveFileEnc(psz_filename, doc, "UTF-8");
	if (nRel != -1)
	{
		xmlFreeDoc(doc);
		return;
	}
}

#define LOAD_NODE(value, node, key, defaultvalue, minvalue, maxvalue) \
	if (!xmlStrcmp(node->name, BAD_CAST(key))) \
	{ \
		int val = defaultvalue;\
		if((node->xmlChildrenNode) && (char*)XML_GET_CONTENT(node->xmlChildrenNode)) \
		val  = atoi((char*)XML_GET_CONTENT(node->xmlChildrenNode)); \
		value = maxvalue != minvalue? MAX(minvalue, MIN(maxvalue, val)) : val;\
	}

#define LOAD_NODED(value, node, key, defaultvalue, minvalue, maxvalue) \
	if (!xmlStrcmp(node->name, BAD_CAST(key))) \
	{ \
		float val = defaultvalue;\
		if((node->xmlChildrenNode) && (char*)XML_GET_CONTENT(node->xmlChildrenNode)) \
		val  = atof((char*)XML_GET_CONTENT(node->xmlChildrenNode)); \
		value = maxvalue != minvalue? MAX(minvalue, MIN(maxvalue, val)) : val;\
	}

#define LOAD_NODES(node, key, defaultvalue, out, i_out) \
	if (!xmlStrcmp(node->name, BAD_CAST(key))) \
	{ \
		strcpy(out, defaultvalue);\
		if((node->xmlChildrenNode)&&(char*)XML_GET_CONTENT(node->xmlChildrenNode))\
		{\
			int len = strlen((char*)(node->xmlChildrenNode)->content);\
			if(len > 0 && len < i_out)\
			strcpy(out, (char*)(node->xmlChildrenNode)->content); \
		}\
	}


void config_LoadConfigFile(encoder_t *p_encoder )
{
	program_t*p_program = &p_encoder->m_program;

	xmlDocPtr pdoc = NULL;
	xmlNodePtr proot = NULL, pcur = NULL;
	xmlKeepBlanksDefault(0);//必须加上，防止程序把元素前后的空白文本符号当作一个node

	char tmp[256];
	strcpy(tmp, p_program->psz_dir);
	strcat(tmp, "config/encoder.xml");
	pdoc = xmlReadFile (tmp, "UTF-8", XML_PARSE_RECOVER);

	if (pdoc == NULL)
	{
		printf ("error:can't open file!\n");
		return;
	}
	/*****************获取xml文档对象的根节对象********************/
	proot = xmlDocGetRootElement (pdoc);
	if (proot == NULL)
	{
		printf("error: file is empty!\n");
		return;
	}

	pcur = proot->xmlChildrenNode;
	while (pcur != NULL)
	{
		if (!xmlStrcmp(pcur->name, BAD_CAST("system")))
		{
			xmlNodePtr nptr=pcur->xmlChildrenNode;
			while (nptr != NULL)
			{
				LOAD_NODED(p_program->f_prob,nptr, "prob", 0.5, 0, 1)
				LOAD_NODE(p_program->i_video_bitrate,nptr, "bitrate", 4000, 1000, 10000)
				LOAD_NODE(p_program->b_compare,nptr, "compare", 0, 0, 1)
				LOAD_NODE(p_program->i_input_type,nptr, "input_type", 1, 0, 2)
				LOAD_NODE(p_program->i_device,nptr, "camera", 0, 0, 1)
				LOAD_NODES(nptr, "input_url", "test.avi", p_program->psz_iurl, 256)
				nptr = nptr->next;
			}
		}

		pcur = pcur->next;
	}

	/*****************释放资源********************/
	xmlFreeDoc (pdoc);
	xmlCleanupParser ();
	xmlMemoryDump ();
}


void close_param(encoder_t *p_encoder)
{
	//msgctl(p_encoder->m_demux.msgrid, IPC_RMID, NULL);
	//msgctl(p_encoder->m_demux.msgwid, IPC_RMID, NULL);

	free(p_encoder);
}

#define LOAD_NODE(js_lists, js_nodes, i_type, bchanged, key) \
    js_nodes = cJSON_GetObjectItem(js_lists, key);\
    if(js_nodes)\
	{\
    	if(i_type != js_nodes->valueint)\
		{\
    		bchanged = 1;\
    		i_type = js_nodes->valueint;\
		}\
	}

#define LOAD_NODED(js_lists, js_nodes, i_type, bchanged, key) \
    js_nodes = cJSON_GetObjectItem(js_lists, key);\
    if(js_nodes)\
	{\
    	if(i_type != js_nodes->valuedouble)\
		{\
    		bchanged = 1;\
    		i_type = js_nodes->valuedouble;\
		}\
	}

#define LOAD_NODES(js_lists, js_nodes, i_type, bchanged, key) \
    js_nodes = cJSON_GetObjectItem(js_lists, key);\
    if(js_nodes)\
	{\
    	if(strcasecmp(i_type,js_nodes->valuestring)) \
		{\
    		bchanged = 1;\
    		strcpy(i_type, js_nodes->valuestring);\
		}\
	}


int set_param(encoder_t *p_encoder, char *psz)
{
	program_t*p_program = &p_encoder->m_program;
	AVElement_t*p_video = &p_encoder->m_video;

	int bchange = 0;

	cJSON *root = cJSON_Parse((char*)psz);
	if(root)
	{
		cJSON *js_node;
		cJSON *js_list = cJSON_GetObjectItem(root, "parameter");
		if(js_list)
		{
			LOAD_NODE(js_list, js_node, p_program->b_image_unet, bchange, "unet")
			LOAD_NODE(js_list, js_node, p_program->i_filter_type, bchange, "filterType")
			LOAD_NODED(js_list, js_node, p_program->i_max, bchange, "hmax")
			LOAD_NODED(js_list, js_node, p_program->i_min, bchange, "hmin")
			LOAD_NODED(js_list, js_node, p_program->f_fps, bchange, "fps")
			LOAD_NODE(js_list, js_node, p_program->b_object_detect, bchange, "objectDetect")
			LOAD_NODE(js_list, js_node, p_program->b_object_show, bchange, "objectShow")
		}

		cJSON_Delete(root);
	}

	if(bchange)
	{
		p_video->b_object_detect = p_program->b_object_detect;
		p_video->b_object_show = p_program->b_object_show;
		p_video->b_image_unet = p_program->b_image_unet;
		p_video->i_filter_type = p_program->i_filter_type;
		p_video->i_max = p_program->i_max;
		p_video->i_min = p_program->i_min;
		p_video->b_compare = p_program->b_compare;
		if(p_video->f_fps != p_program->f_fps)
		{
			p_video->i_frame_period = 1000000.0/p_program->f_fps;
			p_video->f_fps = p_program->f_fps;
			p_video->b_fps_changed = 1;
		}
	}

	return bchange;
}

/*
	printf("*********************** ObjArr *************************\n");
	cJSON * ObjArr = cJSON_GetObjectItem(jsonroot,"pos");
	if (cJSON_IsArray(ObjArr))
	{
		ArrLen = cJSON_GetArraySize(ObjArr);
		printf("ObjArr Len: %d\n", ArrLen);
		for (i = 0; i < ArrLen; i++)
		{
			cJSON * SubObj = cJSON_GetArrayItem(ObjArr, i);
			if(NULL == SubObj)
			{
				continue;
			}

			cJSON_GetObjectItem(SubObj, "prob")->valuedouble;
			cJSON_GetObjectItem(SubObj, "x")->valueint;
			cJSON_GetObjectItem(SubObj, "y")->valueint;
			cJSON_GetObjectItem(SubObj, "w")->valueint;
			cJSON_GetObjectItem(SubObj, "h")->valueint;
		}
	}

*/

void output_roi(AVElement_t*p_video, float *prob)
{
	int h = 64;
	int count = 0;
	cJSON *root = cJSON_CreateObject();
	if(root)
	{
		cJSON * ObjArr = cJSON_CreateArray();
		for (int i = 0; i < 1000; i += 6)
		{
			float conf = prob[i + 4];
			if (conf > p_video->f_prob)
			{
				cJSON * ObjFirst = cJSON_CreateObject();
				cJSON_AddNumberToObject(ObjFirst, "prob", conf);
				cJSON_AddNumberToObject(ObjFirst, "id", (int)(prob[i + 5]));
				cJSON_AddNumberToObject(ObjFirst, "x", (int)(prob[i + 0]));
				cJSON_AddNumberToObject(ObjFirst, "y", (int)(prob[i + 1]-h));
				cJSON_AddNumberToObject(ObjFirst, "w", (int)(prob[i + 2]-prob[i]));
				cJSON_AddNumberToObject(ObjFirst, "h", (int)(prob[i + 3]-prob[i+1]));
				cJSON_AddItemToArray(ObjArr, ObjFirst);
				count++;
			}
		}
		cJSON_AddItemToObject(root, "pos", ObjArr);

		char *s = cJSON_PrintUnformatted(root);
		if(s)
		{
			int len = strlen(s);
			block_t block_out;
			block_out.i_flags = 0;
			block_out.i_buffer = len+1;
			block_out.p_buffer =(uint8_t *) s;
			block_out.i_length = 0;
			block_out.i_pts = 0;
			block_out.i_dts = 0;
			block_out.i_extra = 0;

			BlockBufferWrite(&p_video->video_object, &block_out);

			//printf("%s\n", s);
			free(s);
		}

		cJSON_Delete(root);
	}
}

void output_param(encoder_t *p_encoder, char *psz_cmd)
{
	program_t*p_program = &p_encoder->m_program;

	cJSON *root = cJSON_CreateObject();
	if(root)
	{
		cJSON * js_body ;
		const char *const body = "parameter";

		cJSON_AddItemToObject(root, body, js_body=cJSON_CreateObject());
		cJSON_AddNumberToObject(js_body,"pm_num",1);
		cJSON_AddNumberToObject(js_body,"id",0);
		cJSON_AddNumberToObject(js_body,"unet",p_program->b_image_unet);
		cJSON_AddNumberToObject(js_body,"hmax",p_program->i_max);
		cJSON_AddNumberToObject(js_body,"hmin",p_program->i_min);
		cJSON_AddNumberToObject(js_body,"fps",p_program->f_fps);
		cJSON_AddNumberToObject(js_body,"inputType",p_program->i_input_type);
		cJSON_AddNumberToObject(js_body,"filterType",p_program->i_filter_type);
		cJSON_AddNumberToObject(js_body,"objectDetect",p_program->b_object_detect);
		cJSON_AddNumberToObject(js_body,"objectShow",p_program->b_object_show);
		cJSON_AddStringToObject(js_body,"url", p_program->psz_url);

		char *s = cJSON_PrintUnformatted(root);
		if(s)
		{
			int len = strlen(s);
			memcpy(psz_cmd, s, len);
			psz_cmd[len] = '\n';
			psz_cmd[len+1] = 0;
			free(s);
		}

		cJSON_Delete(root);
	}
}


encoder_t * init_param(int id)
{
	encoder_t *p_encoder = malloc(sizeof(encoder_t));
	program_t *p_program = &p_encoder->m_program;

	memset(p_encoder, 0, sizeof(encoder_t));
	p_encoder->i_id = id;

	char psz_dir[256], *tail;
	memset(psz_dir, 0, 256);
	readlink ("/proc/self/exe", psz_dir, 256);
	tail=strrchr(psz_dir,'/');
	if(tail) *(tail+1) = 0;

#ifdef _DEBUG
	strcpy(psz_dir, "/home/nvidia/encoder/");//test
	//strcpy(psz_dir, "/home/nvidia/Documents/transcoder/Debug/");//test
#endif
	strcpy(p_program->psz_dir, psz_dir);

	p_program->i_video_bitrate = 5000;//kbps
	p_program->i_video_codec  =  ES_V_H264;//ES_V_H265;//ES_V_H264;
	p_program->i_rc_method  =  1;// 0~2
	p_program->i_keyint_max =  30;// 0~255
	p_program->i_depth = 8;
	p_program->f_fps = 30;
	p_program->i_width  = 640;
	p_program->i_height = 512;
	p_program->b_object_detect = 1;
	p_program->b_object_show = 0;
	p_program->i_max = 130;
	p_program->i_min = 20;
	p_program->f_prob = 0.5;
	p_program->i_filter_type = 1;
	p_program->b_image_unet = 0;
	p_program->b_compare = 0;

	strcpy(p_program->psz_iurl, "/home/nvidia/Videos/car5_cut.avi");
	p_program->i_input_type = STRM_IN;//AV_IN;STRM_IN
#ifdef _DEBUG
	p_program->i_device = 1;//camera;file;
#endif
	p_program->i_output_type = URL_OUT;

	//config_WriteConfigFile( p_encoder );
	config_LoadConfigFile(p_encoder );

	return p_encoder;
}
