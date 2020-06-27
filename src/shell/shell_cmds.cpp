/*
 *  Copyright (C) 2002-2020  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *  Wengier: LFN support
 */

#include "shell.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

#include "callback.h"
#include "regs.h"
#include "bios.h"
#include "drives.h"
#include "support.h"
#include "control.h"
#include "paging.h"
#include "drives.h"
#include "../src/ints/int10.h"

extern int enablelfn, lfn_filefind_handle;

// clang-format off
static SHELL_Cmd cmd_list[] = {
	{ "ATTRIB",   1, &DOS_Shell::CMD_ATTRIB,   "SHELL_CMD_ATTRIB_HELP" },
	{ "CALL",     1, &DOS_Shell::CMD_CALL,     "SHELL_CMD_CALL_HELP" },
	{ "CD",       0, &DOS_Shell::CMD_CHDIR,    "SHELL_CMD_CHDIR_HELP" },
	{ "CHDIR",    1, &DOS_Shell::CMD_CHDIR,    "SHELL_CMD_CHDIR_HELP" },
	{ "CHOICE",   1, &DOS_Shell::CMD_CHOICE,   "SHELL_CMD_CHOICE_HELP" },
	{ "CLS",      0, &DOS_Shell::CMD_CLS,      "SHELL_CMD_CLS_HELP" },
	{ "COPY",     0, &DOS_Shell::CMD_COPY,     "SHELL_CMD_COPY_HELP" },
	{ "DATE",     0, &DOS_Shell::CMD_DATE,     "SHELL_CMD_DATE_HELP" },
	{ "DEL",      0, &DOS_Shell::CMD_DELETE,   "SHELL_CMD_DELETE_HELP" },
	{ "DELETE",   1, &DOS_Shell::CMD_DELETE,   "SHELL_CMD_DELETE_HELP" },
	{ "DIR",      0, &DOS_Shell::CMD_DIR,      "SHELL_CMD_DIR_HELP" },
	{ "ECHO",     1, &DOS_Shell::CMD_ECHO,     "SHELL_CMD_ECHO_HELP" },
	{ "ERASE",    1, &DOS_Shell::CMD_DELETE,   "SHELL_CMD_DELETE_HELP" },
	{ "EXIT",     0, &DOS_Shell::CMD_EXIT,     "SHELL_CMD_EXIT_HELP" },
	{ "GOTO",     1, &DOS_Shell::CMD_GOTO,     "SHELL_CMD_GOTO_HELP" },
	{ "HELP",     1, &DOS_Shell::CMD_HELP,     "SHELL_CMD_HELP_HELP" },
	{ "IF",       1, &DOS_Shell::CMD_IF,       "SHELL_CMD_IF_HELP" },
	{ "LH",       1, &DOS_Shell::CMD_LOADHIGH, "SHELL_CMD_LOADHIGH_HELP" },
	{ "LOADHIGH", 1, &DOS_Shell::CMD_LOADHIGH, "SHELL_CMD_LOADHIGH_HELP" },
	{ "LS",       0, &DOS_Shell::CMD_LS,       "SHELL_CMD_LS_HELP" },
	{ "MD",       0, &DOS_Shell::CMD_MKDIR,    "SHELL_CMD_MKDIR_HELP" },
	{ "MKDIR",    1, &DOS_Shell::CMD_MKDIR,    "SHELL_CMD_MKDIR_HELP" },
	{ "PATH",     1, &DOS_Shell::CMD_PATH,     "SHELL_CMD_PATH_HELP" },
	{ "PAUSE",    1, &DOS_Shell::CMD_PAUSE,    "SHELL_CMD_PAUSE_HELP" },
	{ "RD",       0, &DOS_Shell::CMD_RMDIR,    "SHELL_CMD_RMDIR_HELP" },
	{ "REM",      1, &DOS_Shell::CMD_REM,      "SHELL_CMD_REM_HELP" },
	{ "REN",      0, &DOS_Shell::CMD_RENAME,   "SHELL_CMD_RENAME_HELP" },
	{ "RENAME",   1, &DOS_Shell::CMD_RENAME,   "SHELL_CMD_RENAME_HELP" },
	{ "RMDIR",    1, &DOS_Shell::CMD_RMDIR,    "SHELL_CMD_RMDIR_HELP" },
	{ "SET",      1, &DOS_Shell::CMD_SET,      "SHELL_CMD_SET_HELP" },
	{ "SHIFT",    1, &DOS_Shell::CMD_SHIFT,    "SHELL_CMD_SHIFT_HELP" },
	{ "SUBST",    1, &DOS_Shell::CMD_SUBST,    "SHELL_CMD_SUBST_HELP" },
	{ "TIME",     0, &DOS_Shell::CMD_TIME,     "SHELL_CMD_TIME_HELP" },
	{ "TYPE",     0, &DOS_Shell::CMD_TYPE,     "SHELL_CMD_TYPE_HELP" },
	{ "VER",      0, &DOS_Shell::CMD_VER,      "SHELL_CMD_VER_HELP" },
	{ 0, 0, 0, 0 }
};
// clang-format on

/* support functions */
static char empty_char = 0;
static char* empty_string = &empty_char;
static void StripSpaces(char*&args) {
	while (args && *args && isspace(*reinterpret_cast<unsigned char*>(args)))
		args++;
}

static void StripSpaces(char*&args,char also) {
	while (args && *args && (isspace(*reinterpret_cast<unsigned char*>(args)) || (*args == also)))
		args++;
}

static char *ExpandDot(const char *args, char *buffer, size_t bufsize)
{
	if (*args == '.') {
		if (*(args+1) == 0){
			safe_strncpy(buffer, "*.*", bufsize);
			return buffer;
		}
		if ( (*(args+1) != '.') && (*(args+1) != '\\') ) {
			buffer[0] = '*';
			buffer[1] = 0;
			if (bufsize > 2) strncat(buffer,args,bufsize - 1 /*used buffer portion*/ - 1 /*trailing zero*/  );
			return buffer;
		} else
			safe_strncpy (buffer, args, bufsize);
	}
	else safe_strncpy(buffer,args, bufsize);
	return buffer;
}

bool DOS_Shell::CheckConfig(char *cmd_in, char *line) {
	Section* test = control->GetSectionFromProperty(cmd_in);
	if (!test)
		return false;

	if (line && !line[0]) {
		std::string val = test->GetPropValue(cmd_in);
		if (val != NO_SUCH_PROPERTY)
			WriteOut("%s\n", val.c_str());
		return true;
	}
	char newcom[1024];
	snprintf(newcom, sizeof(newcom), "z:\\config -set %s %s%s",
	         test->GetName(),
	         cmd_in,
	         line ? line : "");
	DoCommand(newcom);
	return true;
}

void DOS_Shell::DoCommand(char * line) {
/* First split the line into command and arguments */
	line=trim(line);
	char cmd_buffer[CMD_MAXLINE];
	char * cmd_write=cmd_buffer;
	int q=0;
	while (*line) {
        if (strchr("/\t", *line) || (q / 2 * 2 == q && strchr(" =", *line)))
			break;
		if (*line == '"') q++;
//		if (*line == ':') break; //This breaks drive switching as that is handled at a later stage.
		if ((*line == '.') ||(*line == '\\')) {  //allow stuff like cd.. and dir.exe cd\kees
			*cmd_write=0;
			Bit32u cmd_index=0;
			while (cmd_list[cmd_index].name) {
				if (strcasecmp(cmd_list[cmd_index].name,cmd_buffer) == 0) {
					(this->*(cmd_list[cmd_index].handler))(line);
			 		return;
				}
				cmd_index++;
			}
		}
		*cmd_write++=*line++;
	}
	*cmd_write=0;
	if (strlen(cmd_buffer) == 0) return;
/* Check the internal list */
	Bit32u cmd_index=0;
	while (cmd_list[cmd_index].name) {
		if (strcasecmp(cmd_list[cmd_index].name,cmd_buffer) == 0) {
			(this->*(cmd_list[cmd_index].handler))(line);
			return;
		}
		cmd_index++;
	}
/* This isn't an internal command execute it */
	char ldir[CROSS_LEN], *p=ldir;
	if (strchr(cmd_buffer,'\"')&&DOS_GetSFNPath(cmd_buffer,ldir,false)) {
		if (!strchr(cmd_buffer, '\\') && strrchr(ldir, '\\'))
			p=strrchr(ldir, '\\')+1;
		if (uselfn&&strchr(p, ' ')&&!DOS_FileExists(("\""+std::string(p)+"\"").c_str())) {
			bool append=false;
			if (DOS_FileExists(("\""+std::string(p)+".COM\"").c_str())) {append=true;strcat(p, ".COM");}
			else if (DOS_FileExists(("\""+std::string(p)+".EXE\"").c_str())) {append=true;strcat(p, ".EXE");}
			else if (DOS_FileExists(("\""+std::string(p)+".BAT\"").c_str())) {append=true;strcat(p, ".BAT");}
			if (append&&DOS_GetSFNPath(("\""+std::string(p)+"\"").c_str(), cmd_buffer,false)) if(Execute(cmd_buffer,line)) return;
		}
		if(Execute(p,line)) return;
	} else
		if (Execute(cmd_buffer,line)) return;
	if (CheckConfig(cmd_buffer,line)) return;
	WriteOut(MSG_Get("SHELL_EXECUTE_ILLEGAL_COMMAND"),cmd_buffer);
}

#define HELP(command) \
	if (ScanCMDBool(args,"?")) { \
		WriteOut(MSG_Get("SHELL_CMD_" command "_HELP")); \
		const char* long_m = MSG_Get("SHELL_CMD_" command "_HELP_LONG"); \
		WriteOut("\n"); \
		if (strcmp("Message not Found!\n",long_m)) WriteOut(long_m); \
		else WriteOut(command "\n"); \
		return; \
	}

void DOS_Shell::CMD_CLS(char * args) {
	HELP("CLS");
	reg_ax=0x0003;
	CALLBACK_RunRealInt(0x10);
}

void DOS_Shell::CMD_DELETE(char * args) {
	HELP("DELETE");
	bool optP=ScanCMDBool(args,"P");
	bool optF=ScanCMDBool(args,"F");
	bool optQ=ScanCMDBool(args,"Q");

	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		return;
	}
	if (!*args) {
		WriteOut(MSG_Get("SHELL_MISSING_PARAMETER"));
		return;
	}

	StripSpaces(args);
	args=trim(args);

	/* Command uses dta so set it to our internal dta */
	//DOS_DTA dta(dos.dta());
	RealPt save_dta=dos.dta();
	dos.dta(dos.tables.tempdta);
	DOS_DTA dta(dos.dta());
	/* If delete accept switches mind the space in front of them. See the dir /p code */

	char full[LFN_NAMELENGTH+2],sfull[LFN_NAMELENGTH+2];
	char buffer[CROSS_LEN];
    char name[DOS_NAMELENGTH_ASCII],lname[LFN_NAMELENGTH+1];
    Bit32u size;Bit16u time,date;Bit8u attr;
	args = ExpandDot(args,buffer, CROSS_LEN);
	StripSpaces(args);
	if (!DOS_Canonicalize(args,full)) { WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));dos.dta(save_dta);return; }
	if (strlen(args)&&args[strlen(args)-1]!='\\') {
		Bit16u fattr;
		if (strcmp(args,"*.*")&&DOS_GetFileAttr(args, &fattr) && (fattr&DOS_ATTR_DIRECTORY))
			strcat(args, "\\");
	}
	if (strlen(args)&&args[strlen(args)-1]=='\\') strcat(args, "*.*");
	else if (!strcmp(args,".")||(strlen(args)>1&&(args[strlen(args)-2]==':'||args[strlen(args)-2]=='\\')&&args[strlen(args)-1]=='.')) {
		args[strlen(args)-1]='*';
		strcat(args, ".*");
	} else if (uselfn&&strchr(args, '*')) {
		char * find_last;
		find_last=strrchr(args,'\\');
		if (find_last==NULL) find_last=args;
		else find_last++;
		if (strlen(find_last)>0&&args[strlen(args)-1]=='*'&&strchr(find_last, '.')==NULL) strcat(args, ".*");
	}
	if (!strcmp(args,"*.*")||(strlen(args)>3&&(!strcmp(args+strlen(args)-4, "\\*.*") || !strcmp(args+strlen(args)-4, ":*.*")))) {
		if (!optQ) {
first_1:
			WriteOut(MSG_Get("SHELL_CMD_DEL_SURE"));
first_2:
			Bit8u c;Bit16u n=1;
			DOS_ReadFile (STDIN,&c,&n);
			do switch (c) {
			case 'n':			case 'N':
			{
				DOS_WriteFile (STDOUT,&c, &n);
				DOS_ReadFile (STDIN,&c,&n);
				do switch (c) {
					case 0xD: WriteOut("\n");dos.dta(save_dta);return;
					case 0x03: WriteOut("^C\n");dos.dta(save_dta);return;
					case 0x08: WriteOut("\b \b"); goto first_2;
				} while (DOS_ReadFile (STDIN,&c,&n));
			}
			case 'y':			case 'Y':
			{
				DOS_WriteFile (STDOUT,&c, &n);
				DOS_ReadFile (STDIN,&c,&n);
				do switch (c) {
					case 0xD: WriteOut("\n"); goto continue_1;
					case 0x03: WriteOut("^C\n");dos.dta(save_dta);return;
					case 0x08: WriteOut("\b \b"); goto first_2;
				} while (DOS_ReadFile (STDIN,&c,&n));
			}
			case 0xD: WriteOut("\n"); goto first_1;
			case 0x03: WriteOut("^C\n");dos.dta(save_dta);return;
			case '\t':
			case 0x08:
				goto first_2;
			default:
			{
				DOS_WriteFile (STDOUT,&c, &n);
				DOS_ReadFile (STDIN,&c,&n);
				do switch (c) {
					case 0xD: WriteOut("\n"); goto first_1;
					case 0x03: WriteOut("^C\n");dos.dta(save_dta);return;
					case 0x08: WriteOut("\b \b"); goto first_2;
				} while (DOS_ReadFile (STDIN,&c,&n));
				goto first_2;
			}
		} while (DOS_ReadFile (STDIN,&c,&n));
	}
}

continue_1:
	/* Command uses dta so set it to our internal dta */
	if (!DOS_Canonicalize(args,full)) { WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));dos.dta(save_dta);return; }
	char path[LFN_NAMELENGTH+2], spath[LFN_NAMELENGTH+2], pattern[LFN_NAMELENGTH+2], *r=strrchr(full, '\\');
	if (r!=NULL) {
		*r=0;
		strcpy(path, full);
		strcat(path, "\\");
		strcpy(pattern, r+1);
		*r='\\';
	} else {
		strcpy(path, "");
		strcpy(pattern, full);
	}
	int k=0;
	for (int i=0;i<(int)strlen(pattern);i++)
		if (pattern[i]!='\"')
			pattern[k++]=pattern[i];
	pattern[k]=0;
	strcpy(spath, path);
	if (strchr(args,'\"')||uselfn) {
		if (!DOS_GetSFNPath(("\""+std::string(path)+"\\").c_str(), spath, false)) strcpy(spath, path);
		if (!strlen(spath)||spath[strlen(spath)-1]!='\\') strcat(spath, "\\");
	}
	std::string pfull=std::string(spath)+std::string(pattern);
	int fbak=lfn_filefind_handle;
	lfn_filefind_handle=uselfn?LFN_FILEFIND_INTERNAL:LFN_FILEFIND_NONE;
    bool res=DOS_FindFirst((char *)((uselfn&&pfull.length()&&pfull[0]!='"'?"\"":"")+pfull+(uselfn&&pfull.length()&&pfull[pfull.length()-1]!='"'?"\"":"")).c_str(),0xffff & ~DOS_ATTR_VOLUME);
	if (!res) {
		lfn_filefind_handle=fbak;
		WriteOut(MSG_Get("SHELL_CMD_DEL_ERROR"),args);
		dos.dta(save_dta);
		return;
	}
	lfn_filefind_handle=fbak;
	//end can't be 0, but if it is we'll get a nice crash, who cares :)
	strcpy(sfull,full);
	char * end=strrchr(full,'\\')+1;*end=0;
	char * lend=strrchr(sfull,'\\')+1;*lend=0;
	dta=dos.dta();
	bool exist=false;
	lfn_filefind_handle=uselfn?LFN_FILEFIND_INTERNAL:LFN_FILEFIND_NONE;
	while (res) {
		dta.GetResult(name,lname,size,date,time,attr);
		if (!optF && (attr & DOS_ATTR_READ_ONLY) && !(attr & DOS_ATTR_DIRECTORY)) {
			exist=true;
			strcpy(end,name);
			strcpy(lend,lname);
			WriteOut(MSG_Get("SHELL_CMD_DEL_ERROR"),uselfn?sfull:full);
		} else if (!(attr & DOS_ATTR_DIRECTORY)) {
			exist=true;
			strcpy(end,name);
			strcpy(lend,lname);
			if (optP) {
				WriteOut("Delete %s (Y/N)?", uselfn?sfull:full);
				Bit8u c;
				Bit16u n=1;
				DOS_ReadFile (STDIN,&c,&n);
				if (c==3) {WriteOut("^C\r\n");break;}
				c = c=='y'||c=='Y' ? 'Y':'N';
				WriteOut("%c\r\n", c);
				if (c=='N') {lfn_filefind_handle=uselfn?LFN_FILEFIND_INTERNAL:LFN_FILEFIND_NONE;res = DOS_FindNext();continue;}
			}
			if (strlen(full)) {
				std::string pfull=(uselfn||strchr(full, ' ')?(full[0]!='"'?"\"":""):"")+std::string(full)+(uselfn||strchr(full, ' ')?(full[strlen(full)-1]!='"'?"\"":""):"");
				bool reset=false;
				if (optF && (attr & DOS_ATTR_READ_ONLY)&&DOS_SetFileAttr(pfull.c_str(), attr & ~DOS_ATTR_READ_ONLY)) reset=true;
				if (!DOS_UnlinkFile(pfull.c_str())) {
					if (optF&&reset) DOS_SetFileAttr(pfull.c_str(), attr);
					WriteOut(MSG_Get("SHELL_CMD_DEL_ERROR"),uselfn?sfull:full);
				}
			} else WriteOut(MSG_Get("SHELL_CMD_DEL_ERROR"),uselfn?sfull:full);
		}
		res=DOS_FindNext();
	}
	lfn_filefind_handle=fbak;
	if (!exist) WriteOut(MSG_Get("SHELL_CMD_FILE_NOT_FOUND"),args);
	dos.dta(save_dta);}

void DOS_Shell::CMD_HELP(char * args){
	HELP("HELP");
	bool optall=ScanCMDBool(args,"ALL");
	/* Print the help */
	if (!optall) WriteOut(MSG_Get("SHELL_CMD_HELP"));
	Bit32u cmd_index=0,write_count=0;
	while (cmd_list[cmd_index].name) {
		if (optall || !cmd_list[cmd_index].flags) {
			WriteOut("<\033[34;1m%-8s\033[0m> %s",cmd_list[cmd_index].name,MSG_Get(cmd_list[cmd_index].help));
			if (!(++write_count % 24))
				CMD_PAUSE(empty_string);
		}
		cmd_index++;
	}
}

static void removeChar(char *str, char c) {
    char *src, *dst;
    for (src = dst = str; *src != '\0'; src++) {
        *dst = *src;
        if (*dst != c) dst++;
    }
    *dst = '\0';
}

void DOS_Shell::CMD_RENAME(char * args){
	HELP("RENAME");
	StripSpaces(args);
	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		return;
	}
	if (!*args) {SyntaxError();return;}
	char * arg1=StripArg(args);
	StripSpaces(args);
	if (!*args) {SyntaxError();return;}
	char * arg2=StripArg(args);
	StripSpaces(args);
	if (*args) {SyntaxError();return;}
	char* slash = strrchr(arg1,'\\');
	Bit32u size;Bit16u date;Bit16u time;Bit8u attr;
	char name[DOS_NAMELENGTH_ASCII], lname[LFN_NAMELENGTH+1], tname1[LFN_NAMELENGTH+1], tname2[LFN_NAMELENGTH+1], text1[LFN_NAMELENGTH+1], text2[LFN_NAMELENGTH+1], tfull[CROSS_LEN+2];
	//dir_source and target are introduced for when we support multiple files being renamed.
	char sargs[CROSS_LEN], targs[CROSS_LEN], dir_source[CROSS_LEN + 4] = {0}, dir_target[CROSS_LEN + 4] = {0}, target[CROSS_LEN + 4] = {0}; //not sure if drive portion is included in pathlength

	if (!slash) slash = strrchr(arg1,':');
	if (slash) {
		/* If directory specified (crystal caves installer)
		 * rename from c:\X : rename c:\abc.exe abc.shr.
		 * File must appear in C:\
		 * Ren X:\A\B C => ren X:\A\B X:\A\C */
 
		//Copy first and then modify, makes GCC happy
		safe_strncpy(dir_source,arg1,(uselfn?LFN_NAMELENGTH:DOS_PATHLENGTH) + 4);
		char* dummy = strrchr(dir_source,'\\');
		if (!dummy) dummy = strrchr(dir_source,':');
		if (!dummy) { //Possible due to length
			WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
			return;
		}
		dummy++;
		*dummy = 0;
		if (strchr(arg2,'\\')||strchr(arg2,':')) {
			safe_strncpy(dir_target,arg2,(uselfn?LFN_NAMELENGTH:DOS_PATHLENGTH) + 4);
			dummy = strrchr(dir_target,'\\');
			if (!dummy) dummy = strrchr(dir_target,':');
			if (dummy) {
				dummy++;
				*dummy = 0;
				if (strcasecmp(dir_source, dir_target)) {
					WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
					return;
				}
			}
			arg2=strrchr(arg2,strrchr(arg2,'\\')?'\\':':')+1;
		}
		if (strlen(dummy)&&dummy[strlen(dummy)-1]==':')
			strcat(dummy, ".\\");
	} else {
		if (strchr(arg2,'\\')||strchr(arg2,':')) {SyntaxError();return;};
		strcpy(dir_source, ".\\");
	}

	strcpy(target,arg2);

	char path[LFN_NAMELENGTH+2], spath[LFN_NAMELENGTH+2], pattern[LFN_NAMELENGTH+2], full[LFN_NAMELENGTH+2], *r;
	if (!DOS_Canonicalize(arg1,full)) return;
	r=strrchr(full, '\\');
	if (r!=NULL) {
		*r=0;
		strcpy(path, full);
		strcat(path, "\\");
		strcpy(pattern, r+1);
		*r='\\';
	} else {
		strcpy(path, "");
		strcpy(pattern, full);
	}
	int k=0;
	for (int i=0;i<(int)strlen(pattern);i++)
		if (pattern[i]!='\"')
			pattern[k++]=pattern[i];
	pattern[k]=0;
	strcpy(spath, path);
	if (strchr(arg1,'\"')||uselfn) {
		if (!DOS_GetSFNPath(("\""+std::string(path)+"\\").c_str(), spath, false)) strcpy(spath, path);
		if (!strlen(spath)||spath[strlen(spath)-1]!='\\') strcat(spath, "\\");
	}
	RealPt save_dta=dos.dta();
	dos.dta(dos.tables.tempdta);
	DOS_DTA dta(dos.dta());
	std::string pfull=std::string(spath)+std::string(pattern);
	int fbak=lfn_filefind_handle;
	lfn_filefind_handle=uselfn?LFN_FILEFIND_INTERNAL:LFN_FILEFIND_NONE;
	if (!DOS_FindFirst((char *)((uselfn&&pfull.length()&&pfull[0]!='"'?"\"":"")+pfull+(uselfn&&pfull.length()&&pfull[pfull.length()-1]!='"'?"\"":"")).c_str(), strchr(arg1,'*')!=NULL || strchr(arg1,'?')!=NULL ? 0xffff & ~DOS_ATTR_VOLUME & ~DOS_ATTR_DIRECTORY : 0xffff & ~DOS_ATTR_VOLUME)) {
		lfn_filefind_handle=fbak;
		WriteOut(MSG_Get("SHELL_CMD_RENAME_ERROR"),arg1);
	} else {
		std::vector<std::string> sources;
		sources.clear();

		do {    /* File name and extension */
			dta.GetResult(name,lname,size,date,time,attr);
			lfn_filefind_handle=fbak;

			if(!(attr&DOS_ATTR_DIRECTORY && (!strcmp(name, ".") || !strcmp(name, "..")))) {
				strcpy(dir_target, target);
				removeChar(dir_target, '\"');
				arg2=dir_target;
				strcpy(sargs, dir_source);
				if (uselfn) removeChar(sargs, '\"');
				strcat(sargs, uselfn?lname:name);
				if (uselfn&&strchr(arg2,'*')&&!strchr(arg2,'.')) strcat(arg2, ".*");
				char *dot1=strrchr(uselfn?lname:name,'.'), *dot2=strrchr(arg2,'.'), *star;
				if (dot2==NULL) {
					star=strchr(arg2,'*');
					if (strchr(arg2,'?')) {
						for (unsigned int i=0; i<(uselfn?LFN_NAMELENGTH:DOS_NAMELENGTH) && i<(star?star-arg2:strlen(arg2)); i++) {
							if (*(arg2+i)=='?'&&i<strlen(name))
								*(arg2+i)=name[i];
						}
					}
					if (star) {
						if (star-arg2<(unsigned int)strlen(name))
							strcpy(star, name+(star-arg2));
						else
							*star=0;
					}
					removeChar(arg2, '?');
				} else {
					if (dot1) {
						*dot1=0;
						strcpy(tname1, uselfn?lname:name);
						*dot1='.';
					} else
						strcpy(tname1, uselfn?lname:name);
					*dot2=0;
					strcpy(tname2, arg2);
					*dot2='.';
					star=strchr(tname2,'*');
					if (strchr(tname2,'?')) {
						for (unsigned int i=0; i<(uselfn?LFN_NAMELENGTH:DOS_NAMELENGTH) && i<(star?star-tname2:strlen(tname2)); i++) {
							if (*(tname2+i)=='?'&&i<strlen(tname1))
								*(tname2+i)=tname1[i];
						}
					}
					if (star) {
						if (star-tname2<(unsigned int)strlen(tname1))
							strcpy(star, tname1+(star-tname2));
						else
							*star=0;
					}
					removeChar(tname2, '?');
					if (dot1) {
						strcpy(text1, dot1+1);
						strcpy(text2, dot2+1);
						star=strchr(text2,'*');
						if (strchr(text2,'?')) {
							for (unsigned int i=0; i<(uselfn?LFN_NAMELENGTH:DOS_NAMELENGTH) && i<(star?star-text2:strlen(text2)); i++) {
								if (*(text2+i)=='?'&&i<strlen(text1))
									*(text2+i)=text1[i];
							}
						}
						if (star) {
							if (star-text2<(unsigned int)strlen(text1))
								strcpy(star, text1+(star-text2));
							else
								*star=0;
						}
					} else {
						strcpy(text2, dot2+1);
						if (strchr(text2,'?')||strchr(text2,'*')) {
							for (unsigned int i=0; i<(uselfn?LFN_NAMELENGTH:DOS_NAMELENGTH) && i<(star?star-text2:strlen(text2)); i++) {
								if (*(text2+i)=='*') {
									*(text2+i)=0;
									break;
								}
							}
						}
					}
					removeChar(text2, '?');
					strcpy(tfull, tname2);
					strcat(tfull, ".");
					strcat(tfull, text2);
					arg2=tfull;
				}
				strcpy(targs, dir_source);
				if (uselfn) removeChar(targs, '\"');
				strcat(targs, arg2);
				sources.push_back(uselfn?((sargs[0]!='"'?"\"":"")+std::string(sargs)+(sargs[strlen(sargs)-1]!='"'?"\"":"")).c_str():sargs);
				sources.push_back(uselfn?((targs[0]!='"'?"\"":"")+std::string(targs)+(targs[strlen(targs)-1]!='"'?"\"":"")).c_str():targs);
				sources.push_back(strlen(sargs)>2&&sargs[0]=='.'&&sargs[1]=='\\'?sargs+2:sargs);
			}
			lfn_filefind_handle=uselfn?LFN_FILEFIND_INTERNAL:LFN_FILEFIND_NONE;
		} while ( DOS_FindNext() );
		lfn_filefind_handle=fbak;
		if (sources.empty()) WriteOut(MSG_Get("SHELL_CMD_RENAME_ERROR"),arg1);
		else {
			for (std::vector<std::string>::iterator source = sources.begin(); source != sources.end(); ++source) {
				char *oname=(char *)source->c_str();
				source++;
				if (source==sources.end()) break;
				char *nname=(char *)source->c_str();
				source++;
				if (source==sources.end()||oname==NULL||nname==NULL) break;
				char *fname=(char *)source->c_str();
				if (!DOS_Rename(oname,nname)&&fname!=NULL)
					WriteOut(MSG_Get("SHELL_CMD_RENAME_ERROR"),fname);
			}
		}
	}
	dos.dta(save_dta);}

void DOS_Shell::CMD_ECHO(char * args){
	if (!*args) {
		if (echo) { WriteOut(MSG_Get("SHELL_CMD_ECHO_ON"));}
		else { WriteOut(MSG_Get("SHELL_CMD_ECHO_OFF"));}
		return;
	}
	char buffer[512];
	char* pbuffer = buffer;
	safe_strncpy(buffer,args,512);
	StripSpaces(pbuffer);
	if (strcasecmp(pbuffer,"OFF") == 0) {
		echo=false;
		return;
	}
	if (strcasecmp(pbuffer,"ON") == 0) {
		echo=true;
		return;
	}
	if (strcasecmp(pbuffer,"/?") == 0) { HELP("ECHO"); }

	args++;//skip first character. either a slash or dot or space
	size_t len = strlen(args); //TODO check input of else ook nodig is.
	if (len && args[len - 1] == '\r') {
		LOG(LOG_MISC,LOG_WARN)("Hu ? carriage return already present. Is this possible?");
		WriteOut("%s\n",args);
	} else WriteOut("%s\r\n",args);
}

void DOS_Shell::CMD_EXIT(char *args)
{
	HELP("EXIT");
	exit_flag = true;
}

void DOS_Shell::CMD_CHDIR(char * args) {
	HELP("CHDIR");
	StripSpaces(args);
	char sargs[CROSS_LEN];
	if (*args && !DOS_GetSFNPath(args,sargs,false)) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		return;
	}
	Bit8u drive = DOS_GetDefaultDrive()+'A';
	char dir[LFN_NAMELENGTH+2];
	if (!*args) {
		DOS_GetCurrentDir(0,dir,true);
		WriteOut("%c:\\%s\n",drive,dir);
	} else if (strlen(args) == 2 && args[1] == ':') {
		Bit8u targetdrive = (args[0] | 0x20) - 'a' + 1;
		unsigned char targetdisplay = *reinterpret_cast<unsigned char*>(&args[0]);
		if (!DOS_GetCurrentDir(targetdrive,dir,true)) {
			if (drive == 'Z') {
				WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_NOT_FOUND"),toupper(targetdisplay));
			} else {
				WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
			}
			return;
		}
		WriteOut("%c:\\%s\n",toupper(targetdisplay),dir);
		if (drive == 'Z')
			WriteOut(MSG_Get("SHELL_CMD_CHDIR_HINT"),toupper(targetdisplay));
	} else 	if (!DOS_ChangeDir(sargs)) {
		/* Changedir failed. Check if the filename is longer then 8 and/or contains spaces */

		std::string temps(args),slashpart;
		std::string::size_type separator = temps.find_first_of("\\/");
		if (!separator) {
			slashpart = temps.substr(0,1);
			temps.erase(0,1);
		}
		separator = temps.find_first_of("\\/");
		if (separator != std::string::npos) temps.erase(separator);
		separator = temps.rfind('.');
		if (separator != std::string::npos) temps.erase(separator);
		separator = temps.find(' ');
		if (separator != std::string::npos) {/* Contains spaces */
			temps.erase(separator);
			if (temps.size() >6) temps.erase(6);
			temps += "~1";
			WriteOut(MSG_Get("SHELL_CMD_CHDIR_HINT_2"),temps.insert(0,slashpart).c_str());
		} else if (!uselfn && temps.size()>8) {
			temps.erase(6);
			temps += "~1";
			WriteOut(MSG_Get("SHELL_CMD_CHDIR_HINT_2"),temps.insert(0,slashpart).c_str());
		} else {
			if (drive == 'Z') {
				WriteOut(MSG_Get("SHELL_CMD_CHDIR_HINT_3"));
			} else {
				WriteOut(MSG_Get("SHELL_CMD_CHDIR_ERROR"),args);
			}
		}
	}
}

void DOS_Shell::CMD_MKDIR(char * args) {
	HELP("MKDIR");
	StripSpaces(args);
	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		return;
	}
	if (!DOS_MakeDir(args)) {
		WriteOut(MSG_Get("SHELL_CMD_MKDIR_ERROR"),args);
	}
}

void DOS_Shell::CMD_RMDIR(char * args) {
	HELP("RMDIR");
	StripSpaces(args);
	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		return;
	}
	if (!DOS_RemoveDir(args)) {
		WriteOut(MSG_Get("SHELL_CMD_RMDIR_ERROR"),args);
	}
}

static void FormatNumber(Bit32u num,char * buf) {
	Bit32u numm,numk,numb,numg;
	numb=num % 1000;
	num/=1000;
	numk=num % 1000;
	num/=1000;
	numm=num % 1000;
	num/=1000;
	numg=num;
	if (numg) {
		sprintf(buf,"%d,%03d,%03d,%03d",numg,numm,numk,numb);
		return;
	};
	if (numm) {
		sprintf(buf,"%d,%03d,%03d",numm,numk,numb);
		return;
	};
	if (numk) {
		sprintf(buf,"%d,%03d",numk,numb);
		return;
	};
	sprintf(buf,"%d",numb);
}

struct DtaResult {
	char name[DOS_NAMELENGTH_ASCII];
	char lname[LFN_NAMELENGTH+1];
	Bit32u size;
	Bit16u date;
	Bit16u time;
	Bit8u attr;

	static bool groupDef(const DtaResult &lhs, const DtaResult &rhs) { return (lhs.attr & DOS_ATTR_DIRECTORY) && !(rhs.attr & DOS_ATTR_DIRECTORY)?true:((((lhs.attr & DOS_ATTR_DIRECTORY) && (rhs.attr & DOS_ATTR_DIRECTORY)) || (!(lhs.attr & DOS_ATTR_DIRECTORY) && !(rhs.attr & DOS_ATTR_DIRECTORY))) && strcmp(lhs.name, rhs.name) < 0); }
	static bool groupDirs(const DtaResult &lhs, const DtaResult &rhs) { return (lhs.attr & DOS_ATTR_DIRECTORY) && !(rhs.attr & DOS_ATTR_DIRECTORY); }
	static bool compareName(const DtaResult &lhs, const DtaResult &rhs) { return strcmp(lhs.name, rhs.name) < 0; }
	static bool compareExt(const DtaResult &lhs, const DtaResult &rhs) { return strcmp(lhs.getExtension(), rhs.getExtension()) < 0; }
	static bool compareSize(const DtaResult &lhs, const DtaResult &rhs) { return lhs.size < rhs.size; }
	static bool compareDate(const DtaResult &lhs, const DtaResult &rhs) { return lhs.date < rhs.date || (lhs.date == rhs.date && lhs.time < rhs.time); }

	const char * getExtension() const {
		const char * ext = empty_string;
		if (name[0] != '.') {
			ext = strrchr(name, '.');
			if (!ext) ext = empty_string;
		}
		return ext;
	}

};

/* Unused function
static std::string to_search_pattern(const char *arg)
{
	std::string pattern = arg;
	trim(pattern);

	const char last_char = (pattern.length() > 0 ? pattern.back() : '\0');
	switch (last_char) {
	case '\0': // No arguments, search for all.
		pattern = "*.*";
		break;
	case '\\': // Handle \, C:\, etc.
	case ':':  // Handle C:, etc.
		pattern += "*.*";
		break;
	default: break;
	}

	// Handle patterns starting with a dot.
	char buffer[CROSS_LEN];
	pattern = ExpandDot(pattern.c_str(), buffer, sizeof(buffer));

	// When there's no wildcard and target is a directory then search files
	// inside the directory.
	const char *p = pattern.c_str();
	if (!strrchr(p, '*') && !strrchr(p, '?')) {
		uint16_t attr = 0;
		if (DOS_GetFileAttr(p, &attr) && (attr & DOS_ATTR_DIRECTORY))
			pattern += "\\*.*";
	}

	// If no extension, list all files.
	// This makes patterns like foo* work.
	if (!strrchr(pattern.c_str(), '.'))
		pattern += ".*";

	return pattern;
}
*/

Bit32u byte_count,file_count,dir_count;
Bitu p_count;
std::vector<std::string> dirs, adirs;

static size_t GetPauseCount() {
	Bit8u page=real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
	return (CURSOR_POS_ROW(page) > 2u) ? (CURSOR_POS_ROW(page) - 2u) : 22u; /* <- FIXME: Please clarify this logic */
}

static bool dirPaused(DOS_Shell * shell, Bitu w_size, bool optP, bool optW) {
	p_count+=optW?5:1;
	if (optP && p_count%(GetPauseCount()*w_size)<1) {
		shell->WriteOut(MSG_Get("SHELL_CMD_PAUSE"));
		Bit8u c;Bit16u n=1;
		DOS_ReadFile(STDIN,&c,&n);
		if (c==3) {shell->WriteOut("^C\r\n");return false;}
		if (c==0) DOS_ReadFile(STDIN,&c,&n); // read extended key
	}
	return true;
}

static bool doDir(DOS_Shell * shell, char * args, DOS_DTA dta, char * numformat, Bitu w_size, bool optW, bool optZ, bool optS, bool optP, bool optB, bool optA, bool optAD, bool optAminusD, bool optAS, bool optAminusS, bool optAH, bool optAminusH, bool optAR, bool optAminusR, bool optAA, bool optAminusA, bool optO, bool optOG, bool optON, bool optOD, bool optOE, bool optOS, bool reverseSort) {
	char path[LFN_NAMELENGTH+2];
	char sargs[CROSS_LEN], largs[CROSS_LEN];

	/* Make a full path in the args */
	if (!DOS_Canonicalize(args,path)) {
		shell->WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		return true;
	}
	*(strrchr(path,'\\')+1)=0;
	if (!DOS_GetSFNPath(path,sargs,false)) {
		shell->WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		return true;
	}
    if (!optB&&!optS) {
		shell->WriteOut(MSG_Get("SHELL_CMD_DIR_INTRO"),uselfn&&!optZ&&DOS_GetSFNPath(path,largs,true)?largs:sargs);
		if (optP) {
			p_count+=optW?10:2;
			if (p_count%(GetPauseCount()*w_size)<2) {
				shell->WriteOut(MSG_Get("SHELL_CMD_PAUSE"));
				Bit8u c;Bit16u n=1;
				DOS_ReadFile(STDIN,&c,&n);
				if (c==3) {shell->WriteOut("^C\r\n");return false;}
				if (c==0) DOS_ReadFile(STDIN,&c,&n); // read extended key
			}
		}
	}
    if (*(sargs+strlen(sargs)-1) != '\\') strcat(sargs,"\\");

	Bit32u cbyte_count=0,cfile_count=0,w_count=0;
	int fbak=lfn_filefind_handle;
	lfn_filefind_handle=uselfn&&!optZ?LFN_FILEFIND_INTERNAL:LFN_FILEFIND_NONE;
	bool ret=DOS_FindFirst(args,0xffff & ~DOS_ATTR_VOLUME), found=true, first=true;
	lfn_filefind_handle=fbak;
	if (ret) {
		std::vector<DtaResult> results;

		lfn_filefind_handle=uselfn&&!optZ?LFN_FILEFIND_INTERNAL:LFN_FILEFIND_NONE;
		do {    /* File name and extension */
			DtaResult result;
			dta.GetResult(result.name,result.lname,result.size,result.date,result.time,result.attr);

			/* Skip non-directories if option AD is present, or skip dirs in case of A-D */
			if(optAD && !(result.attr&DOS_ATTR_DIRECTORY) ) continue;
			else if(optAminusD && (result.attr&DOS_ATTR_DIRECTORY) ) continue;
			else if(optAS && !(result.attr&DOS_ATTR_SYSTEM) ) continue;
			else if(optAminusS && (result.attr&DOS_ATTR_SYSTEM) ) continue;
			else if(optAH && !(result.attr&DOS_ATTR_HIDDEN) ) continue;
			else if(optAminusH && (result.attr&DOS_ATTR_HIDDEN) ) continue;
			else if(optAR && !(result.attr&DOS_ATTR_READ_ONLY) ) continue;
			else if(optAminusR && (result.attr&DOS_ATTR_READ_ONLY) ) continue;
			else if(optAA && !(result.attr&DOS_ATTR_ARCHIVE) ) continue;
			else if(optAminusA && (result.attr&DOS_ATTR_ARCHIVE) ) continue;
			else if(!(optA||optAD||optAminusD||optAS||optAminusS||optAH||optAminusH||optAR||optAminusR||optAA||optAminusA) && (result.attr&DOS_ATTR_SYSTEM || result.attr&DOS_ATTR_HIDDEN) && strcmp(result.name, "..") ) continue;

			results.push_back(result);

		} while ( (ret=DOS_FindNext()) );
		lfn_filefind_handle=fbak;

		if (optON) {
			// Sort by name
			std::sort(results.begin(), results.end(), DtaResult::compareName);
		} else if (optOE) {
			// Sort by extension
			std::sort(results.begin(), results.end(), DtaResult::compareExt);
		} else if (optOD) {
			// Sort by date
			std::sort(results.begin(), results.end(), DtaResult::compareDate);
		} else if (optOS) {
			// Sort by size
			std::sort(results.begin(), results.end(), DtaResult::compareSize);
		} else if (optOG) {
			// Directories first, then files
			std::sort(results.begin(), results.end(), DtaResult::groupDirs);
		} else if (optO) {
			// Directories first, then files, both sort by name
			std::sort(results.begin(), results.end(), DtaResult::groupDef);
		}
		if (reverseSort) {
			std::reverse(results.begin(), results.end());
		}

		for (std::vector<DtaResult>::iterator iter = results.begin(); iter != results.end(); ++iter) {

			char * name = iter->name;
			char *lname = iter->lname;
			Bit32u size = iter->size;
			Bit16u date = iter->date;
			Bit16u time = iter->time;
			Bit8u attr = iter->attr;

			/* output the file */
			if (optB) {
				// this overrides pretty much everything
				if (strcmp(".",uselfn&&!optZ?lname:name) && strcmp("..",uselfn&&!optZ?lname:name)) {
					shell->WriteOut("%s\n",uselfn&&!optZ?lname:name);
				}
			} else {
				if (first&&optS) {
					first=false;
					shell->WriteOut("\n");
					shell->WriteOut(MSG_Get("SHELL_CMD_DIR_INTRO"),uselfn&&!optZ&&DOS_GetSFNPath(path,largs,true)?largs:sargs);
					if (optP) {
						p_count+=optW?15:3;
						if (optS&&p_count%(GetPauseCount()*w_size)<3) {
							shell->WriteOut(MSG_Get("SHELL_CMD_PAUSE"));
							Bit8u c;Bit16u n=1;
							DOS_ReadFile(STDIN,&c,&n);
							if (c==3) {shell->WriteOut("^C\r\n");return false;}
							if (c==0) DOS_ReadFile(STDIN,&c,&n); // read extended key
						}
					}
				}
				char * ext = empty_string;
				if (!optW && (name[0] != '.')) {
					ext = strrchr(name, '.');
					if (!ext) ext = empty_string;
					else *ext++ = 0;
				}
				Bit8u day	= (Bit8u)(date & 0x001f);
				Bit8u month	= (Bit8u)((date >> 5) & 0x000f);
				Bit16u year = (Bit16u)((date >> 9) + 1980);
				Bit8u hour	= (Bit8u)((time >> 5 ) >> 6);
				Bit8u minute = (Bit8u)((time >> 5) & 0x003f);

				if (attr & DOS_ATTR_DIRECTORY) {
					if (optW) {
						shell->WriteOut("[%s]",name);
						size_t namelen = strlen(name);
						if (namelen <= 14) {
							for (size_t i=14-namelen;i>0;i--) shell->WriteOut(" ");
						}
					} else {
						shell->WriteOut("%-8s %-3s   %-16s %02d-%02d-%04d %2d:%02d %s\n",name,ext,"<DIR>",day,month,year,hour,minute,uselfn&&!optZ?lname:"");
					}
					dir_count++;
				} else {
					if (optW) {
						shell->WriteOut("%-16s",name);
					} else {
						FormatNumber(size,numformat);
						shell->WriteOut("%-8s %-3s   %16s %02d-%02d-%04d %2d:%02d %s\n",name,ext,numformat,day,month,year,hour,minute,uselfn&&!optZ?lname:"");
					}
					if (optS) {
						cfile_count++;
						cbyte_count+=size;
					}
					file_count++;
					byte_count+=size;
				}
				if (optW) w_count++;
			}
			if (optP && !(++p_count%(GetPauseCount()*w_size))) {
				if (optW&&w_count%5) {shell->WriteOut("\n");w_count=0;}
				shell->WriteOut(MSG_Get("SHELL_CMD_PAUSE"));
				Bit8u c;Bit16u n=1;
				DOS_ReadFile(STDIN,&c,&n);
				if (c==3) {shell->WriteOut("^C\r\n");return false;}
				if (c==0) DOS_ReadFile(STDIN,&c,&n); // read extended key
			}
		}

		if (!results.size())
			found=false;
		else if (optW&&w_count%5)
			shell->WriteOut("\n");
	} else
		found=false;
	if (!found&&!optB&&!optS) {
		shell->WriteOut(MSG_Get("SHELL_CMD_FILE_NOT_FOUND"),args);
		if (!dirPaused(shell, w_size, optP, optW)) return false;
	}
	if (optS) {
		size_t len=strlen(sargs);
		strcat(sargs, "*.*");
		bool ret=DOS_FindFirst(sargs,0xffff & ~DOS_ATTR_VOLUME);
		*(sargs+len)=0;
		if (ret) {
			std::vector<std::string> cdirs;
			cdirs.clear();
			do {    /* File name and extension */
				DtaResult result;
				dta.GetResult(result.name,result.lname,result.size,result.date,result.time,result.attr);

				if(result.attr&DOS_ATTR_DIRECTORY && strcmp(result.name, ".")&&strcmp(result.name, "..")) {
					strcat(sargs, result.name);
					strcat(sargs, "\\");
					char *fname = strrchr(args, '\\');
					if (fname==NULL) fname=args;
					else fname++;
					strcat(sargs, fname);
					cdirs.push_back((sargs[0]!='"'&&sargs[strlen(sargs)-1]=='"'?"\"":"")+std::string(sargs));
					*(sargs+len)=0;
				}
			} while ( (ret=DOS_FindNext()) );
			dirs.insert(dirs.begin()+1, cdirs.begin(), cdirs.end());
		}
		if (found&&!optB) {
			FormatNumber(cbyte_count,numformat);
			shell->WriteOut(MSG_Get("SHELL_CMD_DIR_BYTES_USED"),cfile_count,numformat);
			if (!dirPaused(shell, w_size, optP, optW)) return false;
		}
	}
	return true;
}

void DOS_Shell::CMD_DIR(char * args) {
	HELP("DIR");
	char numformat[16];
	char path[LFN_NAMELENGTH+2];
	char sargs[CROSS_LEN];

	std::string line;
	if(GetEnvStr("DIRCMD",line)){
		std::string::size_type idx = line.find('=');
		std::string value=line.substr(idx +1 , std::string::npos);
		line = std::string(args) + " " + value;
		args=const_cast<char*>(line.c_str());
	}

	ScanCMDBool(args,"4"); /* /4 ignored (default) */
	bool optW=ScanCMDBool(args,"W");
	bool optP=ScanCMDBool(args,"P");
	if (ScanCMDBool(args,"WP") || ScanCMDBool(args,"PW")) optW=optP=true;
	if (ScanCMDBool(args,"-W")) optW=false;
	if (ScanCMDBool(args,"-P")) optP=false;
	bool optZ=ScanCMDBool(args,"Z");
	if (ScanCMDBool(args,"-Z")) optZ=false;
	bool optS=ScanCMDBool(args,"S");
	if (ScanCMDBool(args,"-S")) optS=false;
	bool optB=ScanCMDBool(args,"B");
	if (ScanCMDBool(args,"-B")) optB=false;
	bool optA=ScanCMDBool(args,"A");
	bool optAD=ScanCMDBool(args,"AD")||ScanCMDBool(args,"A:D");
	bool optAminusD=ScanCMDBool(args,"A-D");
	bool optAS=ScanCMDBool(args,"AS")||ScanCMDBool(args,"A:S");
	bool optAminusS=ScanCMDBool(args,"A-S");
	bool optAH=ScanCMDBool(args,"AH")||ScanCMDBool(args,"A:H");
	bool optAminusH=ScanCMDBool(args,"A-H");
	bool optAR=ScanCMDBool(args,"AR")||ScanCMDBool(args,"A:R");
	bool optAminusR=ScanCMDBool(args,"A-R");
	bool optAA=ScanCMDBool(args,"AA")||ScanCMDBool(args,"A:A");
	bool optAminusA=ScanCMDBool(args,"A-A");
	if (ScanCMDBool(args,"-A")) {
		optA = false;
		optAD = false;
		optAminusD = false;
		optAS = false;
		optAminusS = false;
		optAH = false;
		optAminusH = false;
		optAR = false;
		optAminusR = false;
		optAA = false;
		optAminusA = false;
	}
	// Sorting flags
	bool reverseSort = false;
	bool optON=ScanCMDBool(args,"ON")||ScanCMDBool(args,"O:N");
	if (ScanCMDBool(args,"O-N")) {
		optON = true;
		reverseSort = true;
	}
	bool optOD=ScanCMDBool(args,"OD")||ScanCMDBool(args,"O:D");
	if (ScanCMDBool(args,"O-D")) {
		optOD = true;
		reverseSort = true;
	}
	bool optOE=ScanCMDBool(args,"OE")||ScanCMDBool(args,"O:E");
	if (ScanCMDBool(args,"O-E")) {
		optOE = true;
		reverseSort = true;
	}
	bool optOS=ScanCMDBool(args,"OS")||ScanCMDBool(args,"O:S");
	if (ScanCMDBool(args,"O-S")) {
		optOS = true;
		reverseSort = true;
	}
	bool optOG=ScanCMDBool(args,"OG")||ScanCMDBool(args,"O:G");
	if (ScanCMDBool(args,"O-G")) {
		optOG = true;
		reverseSort = true;
	}
	bool optO=ScanCMDBool(args,"O");
	if (ScanCMDBool(args,"OGN")) optO=true;
	if (ScanCMDBool(args,"-O")) {
		optO = false;
		optOG = false;
		optON = false;
		optOD = false;
		optOE = false;
		optOS = false;
		reverseSort = false;
	}
	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		return;
	}
	byte_count=0;file_count=0;dir_count=0;p_count=0;
	Bitu w_size = optW?5:1;

	char buffer[CROSS_LEN];
	args = trim(args);
	size_t argLen = strlen(args);
	if (argLen == 0) {
		strcpy(args,"*.*"); //no arguments.
	} else {
		switch (args[argLen-1])
		{
		case '\\':	// handle \, C:\, etc.
		case ':' :	// handle C:, etc.
			strcat(args,"*.*");
			break;
		default:
			break;
		}
	}
	args = ExpandDot(args,buffer,CROSS_LEN);

	if (DOS_FindDevice(args) != DOS_DEVICES) {
		WriteOut(MSG_Get("SHELL_CMD_FILE_NOT_FOUND"),args);
		return;
	}
	if (!strrchr(args,'*') && !strrchr(args,'?')) {
		Bit16u attribute=0;
		if(!DOS_GetSFNPath(args,sargs,false)) {
			WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
			return;
		}
		if(DOS_GetFileAttr(sargs,&attribute) && (attribute&DOS_ATTR_DIRECTORY) ) {
			DOS_FindFirst(sargs,0xffff & ~DOS_ATTR_VOLUME);
			DOS_DTA dta(dos.dta());
			strcpy(args,sargs);
			strcat(args,"\\*.*");	// if no wildcard and a directory, get its files
		}
	}
	if (!DOS_GetSFNPath(args,sargs,false)) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		return;
	}
	if (!(uselfn&&!optZ&&strchr(sargs,'*'))&&!strrchr(sargs,'.'))
		strcat(sargs,".*");	// if no extension, get them all
    sprintf(args,"\"%s\"",sargs);

	/* Make a full path in the args */
	if (!DOS_Canonicalize(args,path)) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		return;
	}
	*(strrchr(path,'\\')+1)=0;
	if (!DOS_GetSFNPath(path,sargs,true)) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		return;
	}
    if (*(sargs+strlen(sargs)-1) != '\\') strcat(sargs,"\\");

	const char drive_letter = path[0];
	const size_t drive_idx = drive_letter - 'A';
	const bool print_label = (drive_letter >= 'A') && Drives[drive_idx];
    if (!optB) {
		if (print_label) {
			const char *label = Drives[drive_idx]->GetLabel();
			WriteOut(MSG_Get("SHELL_CMD_DIR_VOLUME"), drive_letter, label);
			p_count += 1;
		}
		if (optP) p_count+=optW?15:3;
	}

	/* Command uses dta so set it to our internal dta */
	RealPt save_dta=dos.dta();
	dos.dta(dos.tables.tempdta);
	DOS_DTA dta(dos.dta());
	dirs.clear();
	dirs.push_back(std::string(args));
	while (!dirs.empty()) {
		if (!doDir(this, (char *)dirs.begin()->c_str(), dta, numformat, w_size, optW, optZ, optS, optP, optB, optA, optAD, optAminusD, optAS, optAminusS, optAH, optAminusH, optAR, optAminusR, optAA, optAminusA, optO, optOG, optON, optOD, optOE, optOS, reverseSort)) {dos.dta(save_dta);return;}
		dirs.erase(dirs.begin());
	}
	if (!optB) {
		if (optS) {
			WriteOut("\n");
			if (!dirPaused(this, w_size, optP, optW)) {dos.dta(save_dta);return;}
			if (!file_count&&!dir_count)
				WriteOut(MSG_Get("SHELL_CMD_FILE_NOT_FOUND"),args);
			else
				WriteOut(MSG_Get("SHELL_CMD_DIR_FILES_LISTED"));
			if (!dirPaused(this, w_size, optP, optW)) {dos.dta(save_dta);return;}
		}
		/* Show the summary of results */
		FormatNumber(byte_count,numformat);
		WriteOut(MSG_Get("SHELL_CMD_DIR_BYTES_USED"),file_count,numformat);
		if (!dirPaused(this, w_size, optP, optW)) {dos.dta(save_dta);return;}
		Bit8u drive=dta.GetSearchDrive();
		//TODO Free Space
		Bitu free_space=1024u*1024u*100u;
		if (Drives[drive]) {
			Bit16u bytes_sector;
			Bit8u  sectors_cluster;
			Bit16u total_clusters;
			Bit16u free_clusters;
			Drives[drive]->AllocationInfo(&bytes_sector,
			                              &sectors_cluster,
			                              &total_clusters,
			                              &free_clusters);
			free_space = bytes_sector * sectors_cluster * free_clusters;
		}
		FormatNumber(free_space,numformat);
		WriteOut(MSG_Get("SHELL_CMD_DIR_BYTES_FREE"),dir_count,numformat);
		if (!dirPaused(this, w_size, optP, optW)) {dos.dta(save_dta);return;}
	}
	dos.dta(save_dta);
}

void DOS_Shell::CMD_LS(char *args)
{
	HELP("LS");
	bool optA=ScanCMDBool(args,"A");
	bool optL=ScanCMDBool(args,"L");
	bool optP=ScanCMDBool(args,"P");
	bool optZ=ScanCMDBool(args,"Z");
	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		return;
	}

	RealPt save_dta=dos.dta();
	dos.dta(dos.tables.tempdta);
	DOS_DTA dta(dos.dta());

	std::string pattern = args;
	trim(pattern);

	const char last_char = (pattern.length() > 0 ? pattern.back() : '\0');
	switch (last_char) {
		case '\0': // No arguments, search for all.
			pattern = "*.*";
			break;
		case '\\': // Handle \, C:\, etc.
		case ':':  // Handle C:, etc.
			pattern += "*.*";
			break;
		default: break;
	}

	// Handle patterns starting with a dot.
	char buffer[CROSS_LEN];
	pattern = ExpandDot((char *)pattern.c_str(), buffer, sizeof(buffer));

	// When there's no wildcard and target is a directory then search files
	// inside the directory.
	const char *p = pattern.c_str();
	if (!strrchr(p, '*') && !strrchr(p, '?')) {
		uint16_t attr = 0;
		if (DOS_GetFileAttr(p, &attr) && (attr & DOS_ATTR_DIRECTORY))
			pattern += "\\*.*";
	}

	// If no extension, list all files.
	// This makes patterns like foo* work.
	if (!strrchr(pattern.c_str(), '.'))
		pattern += ".*";

	char spattern[CROSS_LEN];
	if (!DOS_GetSFNPath(pattern.c_str(),spattern,false)) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		return;
	}
	int fbak=lfn_filefind_handle;
	lfn_filefind_handle=uselfn?LFN_FILEFIND_INTERNAL:LFN_FILEFIND_NONE;
	bool ret = DOS_FindFirst((char *)((uselfn?"\"":"")+std::string(spattern)+(uselfn?"\"":"")).c_str(), 0xffff & ~DOS_ATTR_VOLUME);
	if (!ret) {
		lfn_filefind_handle=fbak;
		if (trim(args))
			WriteOut(MSG_Get("SHELL_CMD_FILE_NOT_FOUND"), trim(args));
		else
			WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		dos.dta(save_dta);
		return;
	}

	std::vector<DtaResult> results;
	// reserve space for as many as we can fit into a single memory page
	// nothing more to it; make it larger if necessary
	results.reserve(MEM_PAGE_SIZE / sizeof(DtaResult));

	do {
		DtaResult result;
		dta.GetResult(result.name, result.lname, result.size, result.date, result.time, result.attr);
		results.push_back(result);
	} while ((ret = DOS_FindNext()) == true);
	lfn_filefind_handle=fbak;

	size_t w_count, p_count, col;
	unsigned int max[10], total, tcols=real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
	if (!tcols) tcols=80;

	for (col=10; col>0; col--) {
		for (int i=0; i<10; i++) max[i]=2;
		if (optL) col=1;
		if (col==1) break;
		w_count=0;
		for (const auto &entry : results) {
			std::string name = uselfn&&!optZ?entry.lname:entry.name;
			if (name == "." || name == "..") continue;
			if (!optA && (entry.attr&DOS_ATTR_SYSTEM || entry.attr&DOS_ATTR_HIDDEN)) continue;
			if (name.size()+2>max[w_count%col]) max[w_count%col]=(unsigned int)(name.size()+2);
			++w_count;
		}
		total=0;
		for (size_t i=0; i<col; i++) total+=max[i];
		if (total<tcols) break;
	}

	w_count = 0, p_count = 0;

	for (const auto &entry : results) {
		std::string name = uselfn&&!optZ?entry.lname:entry.name;
		if (name == "." || name == "..") continue;
		if (!optA && (entry.attr&DOS_ATTR_SYSTEM || entry.attr&DOS_ATTR_HIDDEN)) continue;
		if (entry.attr & DOS_ATTR_DIRECTORY) {
			if (!uselfn||optZ) upcase(name);
			if (col==1) {
				WriteOut("\033[34;1m%s\033[0m\n", name.c_str());
				p_count++;
			} else
				WriteOut("\033[34;1m%-*s\033[0m", max[w_count % col], name.c_str());
		} else {
			if (!uselfn||optZ) lowcase(name);
			const bool is_executable = name.length()>4 && (!strcasecmp(name.substr(name.length()-4).c_str(), ".exe") || !strcasecmp(name.substr(name.length()-4).c_str(), ".com") || !strcasecmp(name.substr(name.length()-4).c_str(), ".bat"));
			if (col==1) {
				WriteOut(is_executable?"\033[32;1m%s\033[0m\n":"%s\n", name.c_str());
				p_count++;
			} else
				WriteOut(is_executable?"\033[32;1m%-*s\033[0m":"%-*s", max[w_count % col], name.c_str());
		}
		if (col>1) {
			++w_count;
			if (w_count % col == 0) {p_count++;WriteOut_NoParsing("\n");}
		}
		if (optP&&p_count>=GetPauseCount()) {
			WriteOut(MSG_Get("SHELL_CMD_PAUSE"));
			Bit8u c;Bit16u n=1;
			DOS_ReadFile(STDIN,&c,&n);
			if (c==3) {WriteOut("^C\r\n");dos.dta(save_dta);return;}
			if (c==0) DOS_ReadFile(STDIN,&c,&n); // read extended key
			p_count=0;
		}
	}
	if (col>1&&w_count%col) WriteOut_NoParsing("\n");
	dos.dta(save_dta);
}

struct copysource {
	std::string filename;
	bool concat;
	copysource(std::string filein,bool concatin):
		filename(filein),concat(concatin){ };
	copysource():filename(""),concat(false){ };
};


void DOS_Shell::CMD_COPY(char * args) {
	static std::string defaulttarget = ".";
	StripSpaces(args);
	/* Command uses dta so set it to our internal dta */
	RealPt save_dta=dos.dta();
	dos.dta(dos.tables.tempdta);
	DOS_DTA dta(dos.dta());
	Bit32u size;Bit16u date;Bit16u time;Bit8u attr;
	char name[DOS_NAMELENGTH_ASCII], lname[LFN_NAMELENGTH+1];
	std::vector<copysource> sources;
	// ignore /b and /t switches: always copy binary
	while(ScanCMDBool(args,"B")) ;
	while(ScanCMDBool(args,"T")) ; //Shouldn't this be A ?
	while(ScanCMDBool(args,"A")) ;
	bool optY=ScanCMDBool(args,"Y");
	std::string line;
	if(GetEnvStr("COPYCMD",line)){
		std::string::size_type idx = line.find('=');
		std::string value=line.substr(idx +1 , std::string::npos);
		char copycmd[CROSS_LEN];
		strcpy(copycmd, value.c_str());
		if (ScanCMDBool(copycmd, "Y") && !ScanCMDBool(copycmd, "-Y")) optY = true;
	}
	if (ScanCMDBool(args,"-Y")) optY=false;
	ScanCMDBool(args,"V");

	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		dos.dta(save_dta);
		return;
	}
	// Gather all sources (extension to copy more then 1 file specified at command line)
	// Concatenating files go as follows: All parts except for the last bear the concat flag.
	// This construction allows them to be counted (only the non concat set)
	char q[]="\"";
	char* source_p = NULL;
	char source_x[DOS_PATHLENGTH+CROSS_LEN];
	while ( (source_p = StripArg(args)) && *source_p ) {
		do {
			char* plus = strchr(source_p,'+');
			// If StripWord() previously cut at a space before a plus then
			// set concatenate flag on last source and remove leading plus.
			if (plus == source_p && sources.size()) {
				sources[sources.size()-1].concat = true;
				// If spaces also followed plus then item is only a plus.
				if (strlen(++source_p)==0) break;
				plus = strchr(source_p,'+');
			}
			if (plus) {
				char *c=source_p+strlen(source_p)-1;
				if (*source_p=='"'&&*c=='"') {
					*c=0;
					if (strchr(source_p+1,'"'))
						*plus++ = 0;
					else
						plus=NULL;
					*c='"';
				} else
					*plus++ = 0;
			}
			safe_strncpy(source_x,source_p,CROSS_LEN);
			bool has_drive_spec = false;
			size_t source_x_len = strlen(source_x);
			if (source_x_len>0) {
				if (source_x[source_x_len-1]==':') has_drive_spec = true;
				else if (uselfn&&strchr(source_x, '*')) {
					char * find_last;
					find_last=strrchr(source_x,'\\');
					if (find_last==NULL) find_last=source_x;
					else find_last++;
					if (strlen(find_last)>0&&source_x[source_x_len-1]=='*'&&strchr(find_last, '.')==NULL) strcat(source_x, ".*");
				}
			}
			if (!has_drive_spec  && !strpbrk(source_p,"*?") ) { //doubt that fu*\*.* is valid
                char spath[DOS_PATHLENGTH+2];
                if (DOS_GetSFNPath(source_p,spath,false)) {
					bool root=false;
					if (strlen(spath)==3&&spath[1]==':'&&spath[2]=='\\') {
						root=true;
						strcat(spath, "*.*");
					}
					if (DOS_FindFirst(spath,0xffff & ~DOS_ATTR_VOLUME)) {
                    dta.GetResult(name,lname,size,date,time,attr);
					if (attr & DOS_ATTR_DIRECTORY || root)
						strcat(source_x,"\\*.*");
					}
				}
			}
            std::string source_xString = std::string(source_x);
			sources.push_back(copysource(source_xString,(plus)?true:false));
			source_p = plus;
		} while (source_p && *source_p);
	}
	// At least one source has to be there
	if (!sources.size() || !sources[0].filename.size()) {
		WriteOut(MSG_Get("SHELL_MISSING_PARAMETER"));
		dos.dta(save_dta);
		return;
	}

	copysource target;
	// If more then one object exists and last target is not part of a
	// concat sequence then make it the target.
	if (sources.size() > 1 && !sources[sources.size() - 2].concat){
		target = sources.back();
		sources.pop_back();
	}
	//If no target => default target with concat flag true to detect a+b+c
	if (target.filename.size() == 0) target = copysource(defaulttarget,true);

	copysource oldsource;
	copysource source;
	Bit32u count = 0;
	while(sources.size()) {
		/* Get next source item and keep track of old source for concat start end */
		oldsource = source;
		source = sources[0];
		sources.erase(sources.begin());

		//Skip first file if doing a+b+c. Set target to first file
		if(!oldsource.concat && source.concat && target.concat) {
			target = source;
			continue;
		}

		/* Make a full path in the args */
		char pathSourcePre[LFN_NAMELENGTH], pathSource[LFN_NAMELENGTH+2];
		char pathTarget[LFN_NAMELENGTH+2];

		if (!DOS_Canonicalize(const_cast<char*>(source.filename.c_str()),pathSourcePre)) {
			WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
			dos.dta(save_dta);
			return;
		}
		strcpy(pathSource,pathSourcePre);
		if (uselfn) sprintf(pathSource,"\"%s\"",pathSourcePre);
		// cut search pattern
		char* pos = strrchr(pathSource,'\\');
		if (pos) *(pos+1) = 0;

		if (!DOS_Canonicalize(const_cast<char*>(target.filename.c_str()),pathTarget)) {
			WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
			dos.dta(save_dta);
			return;
		}
		char* temp = strstr(pathTarget,"*.*");
		if(temp && (temp == pathTarget || temp[-1] == '\\')) *temp = 0;//strip off *.* from target

		// add '\\' if target is a directory
		bool target_is_file = true;
		if (pathTarget[strlen(pathTarget)-1]!='\\') {
			if (DOS_FindFirst(pathTarget,0xffff & ~DOS_ATTR_VOLUME)) {
				dta.GetResult(name,lname,size,date,time,attr);
				if (attr & DOS_ATTR_DIRECTORY) {
					strcat(pathTarget,"\\");
					target_is_file = false;
				}
			}
		} else target_is_file = false;

		//Find first sourcefile
		char sPath[LFN_NAMELENGTH+2];
		bool ret=DOS_GetSFNPath(source.filename.c_str(),sPath,false) && DOS_FindFirst((char *)((strchr(sPath, ' ')&&sPath[0]!='"'&&sPath[0]!='"'?"\"":"")+std::string(sPath)+(strchr(sPath, ' ')&&sPath[strlen(sPath)-1]!='"'?"\"":"")).c_str(),0xffff & ~DOS_ATTR_VOLUME);
		if (!ret) {
			WriteOut(MSG_Get("SHELL_CMD_FILE_NOT_FOUND"),const_cast<char*>(source.filename.c_str()));
			dos.dta(save_dta);
			return;
		}

		Bit16u sourceHandle,targetHandle = 0;
		char nameTarget[LFN_NAMELENGTH];
		char nameSource[LFN_NAMELENGTH], nametmp[DOS_PATHLENGTH+2];

		// Cache so we don't have to recalculate
		size_t pathTargetLen = strlen(pathTarget);

		// See if we have to substitute filename or extension
		char *ext = 0;
		size_t replacementOffset = 0;
		if (pathTarget[pathTargetLen-1]!='\\') {
				// only if it's not a directory
			ext = strchr(pathTarget, '.');
			if (ext > pathTarget) { // no possible substitution
				if (ext[-1] == '*') {
					// we substitute extension, save it, hide the name
					ext[-1] = 0;
					assert(ext > pathTarget + 1); // pathTarget is fully qualified
					if (ext[-2] != '\\') {
						// there is something before the asterisk
						// save the offset in the source names

						replacementOffset = source.filename.find('*');
						size_t lastSlash = source.filename.rfind('\\');
						if (std::string::npos == lastSlash)
							lastSlash = 0;
						else
							lastSlash++;
						if (std::string::npos == replacementOffset
							  || replacementOffset < lastSlash) {
							// no asterisk found or in wrong place, error
							WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
							dos.dta(save_dta);
							return;
						}
						replacementOffset -= lastSlash;
//						WriteOut("[II] replacement offset is %d\n", replacementOffset);
					}
				}
				if (ext[1] == '*') {
					// we substitute name, clear the extension
					*ext = 0;
				} else if (ext[-1]) {
					// we don't substitute anything, clear up
					ext = 0;
				}
			}
		}

		bool echo=dos.echo, second_file_of_current_source = false;
		while (ret) {
			dta.GetResult(name,lname,size,date,time,attr);

			if ((attr & DOS_ATTR_DIRECTORY)==0) {
                Bit16u ftime,fdate;

				strcpy(nameSource,pathSource);
				strcat(nameSource,name);

				// Open Source
				if (DOS_OpenFile(nameSource,0,&sourceHandle)) {
                    // record the file date/time
                    bool ftdvalid = DOS_GetFileDate(sourceHandle, &ftime, &fdate);
                    if (!ftdvalid) LOG_MSG("WARNING: COPY cannot obtain file date/time");

					// Create Target or open it if in concat mode
					strcpy(nameTarget,q);
                    strcat(nameTarget,pathTarget);

					if (ext) { // substitute parts if necessary
						if (!ext[-1]) { // substitute extension
							strcat(nameTarget, (uselfn?lname:name) + replacementOffset);
							char *p=strchr(nameTarget, '.');
							strcpy(p==NULL?nameTarget+strlen(nameTarget):p, ext);
						}
						if (ext[1] == '*') { // substitute name (so just add the extension)
							strcat(nameTarget, strchr(uselfn?lname:name, '.'));
						}
					}

                    if (nameTarget[strlen(nameTarget)-1]=='\\') strcat(nameTarget,uselfn?lname:name);
                    strcat(nameTarget,q);

					//Special variable to ensure that copy * a_file, where a_file is not a directory concats.
					bool special = second_file_of_current_source && target_is_file && strchr(target.filename.c_str(), '*')==NULL;
					second_file_of_current_source = true;
					if (special) oldsource.concat = true;
					if (*nameSource&&*nameTarget) {
						strcpy(nametmp, nameSource[0]!='\"'&&nameTarget[0]=='\"'?"\"":"");
						strcat(nametmp, nameSource);
						strcat(nametmp, nameSource[strlen(nameSource)-1]!='\"'&&nameTarget[strlen(nameTarget)-1]=='\"'?"\"":"");
					} else
						strcpy(nametmp, nameSource);
					if (!oldsource.concat && (!strcasecmp(nameSource, nameTarget) || !strcasecmp(nametmp, nameTarget)))
						{
						WriteOut("File cannot be copied onto itself\r\n");
						dos.dta(save_dta);
						DOS_CloseFile(sourceHandle);
						if (targetHandle)
							DOS_CloseFile(targetHandle);
						return;
						}
					Bit16u fattr;
					bool exist = DOS_GetFileAttr(nameTarget, &fattr);
					if (!(attr & DOS_ATTR_DIRECTORY) && DOS_FindDevice(nameTarget) == DOS_DEVICES) {
						if (exist && !optY && !oldsource.concat) {
							dos.echo=false;
							WriteOut(MSG_Get("SHELL_CMD_COPY_CONFIRM"), nameTarget);
							Bit8u c;
							Bit16u n=1;
							while (true)
								{
								DOS_ReadFile (STDIN,&c,&n);
								if (c==3) {WriteOut("^C\r\n");dos.dta(save_dta);DOS_CloseFile(sourceHandle);dos.echo=echo;return;}
								if (c=='y'||c=='Y') {WriteOut("Y\r\n", c);break;}
								if (c=='n'||c=='N') {WriteOut("N\r\n", c);break;}
								if (c=='a'||c=='A') {WriteOut("A\r\n", c);optY=true;break;}
								}
							if (c=='n'||c=='N') {DOS_CloseFile(sourceHandle);ret = DOS_FindNext();continue;}
						}
					}
					//Don't create a new file when in concat mode
					if (oldsource.concat || DOS_CreateFile(nameTarget,0,&targetHandle)) {
						Bit32u dummy=0;

						//In concat mode. Open the target and seek to the eof
						if (!oldsource.concat || (DOS_OpenFile(nameTarget,OPEN_READWRITE,&targetHandle) &&
					        	                  DOS_SeekFile(targetHandle,&dummy,DOS_SEEK_END))) {
							// Copy
							static Bit8u buffer[0x8000]; // static, otherwise stack overflow possible.
							bool	failed = false;
							Bit16u	toread = 0x8000;
							bool iscon=DOS_FindDevice(name)==DOS_FindDevice("con");
							if (iscon) dos.echo=true;
							bool cont;
							do {
								if (!DOS_ReadFile(sourceHandle,buffer,&toread)) failed=true;
								if (iscon)
									{
									if (dos.errorcode==77)
										{
										WriteOut("^C\r\n");
										dos.dta(save_dta);
										DOS_CloseFile(sourceHandle);
										DOS_CloseFile(targetHandle);
										if (!exist) DOS_UnlinkFile(nameTarget);
										dos.echo=echo;
										return;
										}
									cont=true;
									for (int i=0;i<toread;i++)
										if (buffer[i]==26)
											{
											toread=i;
											cont=false;
											break;
											}
									if (!DOS_WriteFile(targetHandle,buffer,&toread)) failed=true;
									if (cont) toread=0x8000;
									}
								else
									{
									if (!DOS_WriteFile(targetHandle,buffer,&toread)) failed=true;
									cont=toread == 0x8000;
									}
							} while (cont);
							if (!DOS_CloseFile(sourceHandle)) failed=true;
							if (!DOS_CloseFile(targetHandle)) failed=true;
							if (failed)
                                WriteOut(MSG_Get("SHELL_CMD_COPY_ERROR"),uselfn?lname:name);
                            else if (strcmp(name,lname)&&uselfn)
                                WriteOut(" %s [%s]\n",lname,name);
                            else
                                WriteOut(" %s\n",uselfn?lname:name);
							if(!source.concat && !special) count++; //Only count concat files once
						} else {
							DOS_CloseFile(sourceHandle);
							WriteOut(MSG_Get("SHELL_CMD_COPY_FAILURE"),const_cast<char*>(target.filename.c_str()));
						}
					} else {
						DOS_CloseFile(sourceHandle);
						WriteOut(MSG_Get("SHELL_CMD_COPY_FAILURE"),const_cast<char*>(target.filename.c_str()));
					}
				} else WriteOut(MSG_Get("SHELL_CMD_COPY_FAILURE"),const_cast<char*>(source.filename.c_str()));
			}
			//On to the next file if the previous one wasn't a device
			if ((attr&DOS_ATTR_DEVICE) == 0) ret = DOS_FindNext();
			else ret = false;
		}
	}

	WriteOut(MSG_Get("SHELL_CMD_COPY_SUCCESS"),count);
	dos.dta(save_dta);
	dos.echo=echo;
	Drives[DOS_GetDefaultDrive()]->EmptyCache();
}

void DOS_Shell::CMD_SET(char * args) {
	HELP("SET");
	StripSpaces(args);
	std::string line;
	if (!*args) {
		/* No command line show all environment lines */
		Bitu count=GetEnvCount();
		for (Bitu a=0;a<count;a++) {
			if (GetEnvNum(a,line)) WriteOut("%s\n",line.c_str());
		}
		return;
	}
	//There are args:
	char * pcheck = args;
	while ( *pcheck && (*pcheck == ' ' || *pcheck == '\t')) pcheck++;
	if (*pcheck && strlen(pcheck) >3 && (strncasecmp(pcheck,"/p ",3) == 0)) E_Exit("Set /P is not supported. Use Choice!");

	char * p=strpbrk(args, "=");
	if (!p) {
		if (!GetEnvStr(args,line)) WriteOut(MSG_Get("SHELL_CMD_SET_NOT_SET"),args);
		WriteOut("%s\n",line.c_str());
	} else {
		*p++=0;
		/* parse p for envirionment variables */
		char parsed[CMD_MAXLINE];
		char* p_parsed = parsed;
		while (*p) {
			if (*p != '%') *p_parsed++ = *p++; //Just add it (most likely path)
			else if ( *(p+1) == '%') {
				*p_parsed++ = '%'; p += 2; //%% => %
			} else {
				char * second = strchr(++p,'%');
				if (!second) continue;
				*second++ = 0;
				std::string temp;
				if (GetEnvStr(p,temp)) {
					std::string::size_type equals = temp.find('=');
					if (equals == std::string::npos)
						continue;
					const uintptr_t remaining_len = std::min(
					        sizeof(parsed) - static_cast<uintptr_t>(p_parsed - parsed),
					        sizeof(parsed));
					safe_strncpy(p_parsed,
					             temp.substr(equals + 1).c_str(),
					             remaining_len);
					p_parsed += strlen(p_parsed);
				}
				p = second;
			}
		}
		*p_parsed = 0;
		/* Try setting the variable */
		if (!SetEnv(args,parsed)) {
			WriteOut(MSG_Get("SHELL_CMD_SET_OUT_OF_SPACE"));
		}
	}
}

void DOS_Shell::CMD_IF(char * args) {
	HELP("IF");
	StripSpaces(args,'=');
	bool has_not=false;

	while (strncasecmp(args,"NOT",3) == 0) {
		if (!isspace(*reinterpret_cast<unsigned char*>(&args[3])) && (args[3] != '=')) break;
		args += 3;	//skip text
		//skip more spaces
		StripSpaces(args,'=');
		has_not = !has_not;
	}

	if (strncasecmp(args,"ERRORLEVEL",10) == 0) {
		args += 10;	//skip text
		//Strip spaces and ==
		StripSpaces(args,'=');
		char* word = StripWord(args);
		if (!isdigit(*word)) {
			WriteOut(MSG_Get("SHELL_CMD_IF_ERRORLEVEL_MISSING_NUMBER"));
			return;
		}

		Bit8u n = 0;
		do n = n * 10 + (*word - '0');
		while (isdigit(*++word));
		if (*word && !isspace(*word)) {
			WriteOut(MSG_Get("SHELL_CMD_IF_ERRORLEVEL_INVALID_NUMBER"));
			return;
		}
		/* Read the error code from DOS */
		if ((dos.return_code>=n) ==(!has_not)) DoCommand(args);
		return;
	}

	if(strncasecmp(args,"EXIST ",6) == 0) {
		args += 6; //Skip text
		StripSpaces(args);
		char* word = StripArg(args);
		if (!*word) {
			WriteOut(MSG_Get("SHELL_CMD_IF_EXIST_MISSING_FILENAME"));
			return;
		}

		{	/* DOS_FindFirst uses dta so set it to our internal dta */
			char spath[LFN_NAMELENGTH+2], path[LFN_NAMELENGTH+2], pattern[LFN_NAMELENGTH+2], full[LFN_NAMELENGTH+2], *r;
			if (!DOS_Canonicalize(word,full)) return;
			r=strrchr(full, '\\');
			if (r!=NULL) {
				*r=0;
				strcpy(path, full);
				strcat(path, "\\");
				strcpy(pattern, r+1);
				*r='\\';
			} else {
				strcpy(path, "");
				strcpy(pattern, full);
			}
			int k=0;
			for (int i=0;i<(int)strlen(pattern);i++)
				if (pattern[i]!='\"')
					pattern[k++]=pattern[i];
			pattern[k]=0;
			strcpy(spath, path);
			if (strchr(word,'\"')||uselfn) {
				if (!DOS_GetSFNPath(("\""+std::string(path)+"\\").c_str(), spath, false)) strcpy(spath, path);
				if (!strlen(spath)||spath[strlen(spath)-1]!='\\') strcat(spath, "\\");
			}
			RealPt save_dta=dos.dta();
			dos.dta(dos.tables.tempdta);
			int fbak=lfn_filefind_handle;
			lfn_filefind_handle=uselfn?LFN_FILEFIND_INTERNAL:LFN_FILEFIND_NONE;
			std::string sfull=std::string(spath)+std::string(pattern);
			bool ret=DOS_FindFirst((char *)((uselfn&&sfull.length()&&sfull[0]!='"'?"\"":"")+sfull+(uselfn&&sfull.length()&&sfull[sfull.length()-1]!='"'?"\"":"")).c_str(),0xffff & ~(DOS_ATTR_VOLUME|DOS_ATTR_DIRECTORY));
			lfn_filefind_handle=fbak;
			dos.dta(save_dta);
			if (ret==(!has_not)) DoCommand(args);
		}
		return;
	}

	/* Normal if string compare */

	char* word1 = args;
	// first word is until space or =
	while (*args && !isspace(*reinterpret_cast<unsigned char*>(args)) && (*args != '='))
		args++;
	char* end_word1 = args;

	// scan for =
	while (*args && (*args != '='))
		args++;
	// check for ==
	if ((*args == 0) || (args[1] != '=')) {
		SyntaxError();
		return;
	}
	args += 2;
	StripSpaces(args,'=');

	char* word2 = args;
	// second word is until space or =
	while (*args && !isspace(*reinterpret_cast<unsigned char*>(args)) && (*args != '='))
		args++;

	if (*args) {
		*end_word1 = 0;		// mark end of first word
		*args++ = 0;		// mark end of second word
		StripSpaces(args,'=');

		if ((strcmp(word1,word2) == 0) == (!has_not)) DoCommand(args);
	}
}

void DOS_Shell::CMD_GOTO(char * args) {
	HELP("GOTO");
	StripSpaces(args);
	if (!bf) return;
	if (*args &&(*args == ':')) args++;
	//label ends at the first space
	char* non_space = args;
	while (*non_space) {
		if ((*non_space == ' ') || (*non_space == '\t'))
			*non_space = 0;
		else non_space++;
	}
	if (!*args) {
		WriteOut(MSG_Get("SHELL_CMD_GOTO_MISSING_LABEL"));
		return;
	}
	if (!bf->Goto(args)) {
		WriteOut(MSG_Get("SHELL_CMD_GOTO_LABEL_NOT_FOUND"),args);
		return;
	}
}

void DOS_Shell::CMD_SHIFT(char * args ) {
	HELP("SHIFT");
	if (bf) bf->Shift();
}

void DOS_Shell::CMD_TYPE(char * args) {
	HELP("TYPE");
	StripSpaces(args);
	if (!*args) {
		WriteOut(MSG_Get("SHELL_SYNTAXERROR"));
		return;
	}
	Bit16u handle;
	char * word;
nextfile:
	word=StripArg(args);
	if (!DOS_OpenFile(word,0,&handle)) {
		WriteOut(MSG_Get("SHELL_CMD_FILE_NOT_FOUND"),word);
		return;
	}
	Bit8u c;Bit16u n=1;
	bool iscon=DOS_FindDevice(word)==DOS_FindDevice("con");
	while (n) {
		DOS_ReadFile(handle,&c,&n);
		if (n==0 || c==0x1a) break; // stop at EOF
		if (iscon) {
			if (c==3) {WriteOut("^C\r\n");break;}
			else if (c==13) WriteOut("\r\n");
		}
		DOS_WriteFile(STDOUT,&c,&n);
	}
	DOS_CloseFile(handle);
	WriteOut("\r\n");
	if (*args) goto nextfile;
}

void DOS_Shell::CMD_REM(char * args) {
	HELP("REM");
}

void DOS_Shell::CMD_PAUSE(char *args) {
	HELP("PAUSE");
	WriteOut(MSG_Get("SHELL_CMD_PAUSE"));
	uint8_t c;
	uint16_t n = 1;
	DOS_ReadFile(STDIN, &c, &n);
	if (c == 0)
		DOS_ReadFile(STDIN, &c, &n); // read extended key
	WriteOut_NoParsing("\n");
}

void DOS_Shell::CMD_CALL(char * args){
	HELP("CALL");
	this->call=true; /* else the old batchfile will be closed first */
	this->ParseLine(args);
	this->call=false;
}

void DOS_Shell::CMD_DATE(char * args) {
	HELP("DATE");
	if (ScanCMDBool(args,"H")) {
		// synchronize date with host parameter
		time_t curtime;
		struct tm *loctime;
		curtime = time (NULL);
		loctime = localtime (&curtime);

		reg_cx = loctime->tm_year+1900;
		reg_dh = loctime->tm_mon+1;
		reg_dl = loctime->tm_mday;

		reg_ah=0x2b; // set system date
		CALLBACK_RunRealInt(0x21);
		return;
	}
	// check if a date was passed in command line
	Bit32u newday,newmonth,newyear;
	if (sscanf(args,"%u-%u-%u",&newmonth,&newday,&newyear) == 3) {
		reg_cx = static_cast<Bit16u>(newyear);
		reg_dh = static_cast<Bit8u>(newmonth);
		reg_dl = static_cast<Bit8u>(newday);

		reg_ah=0x2b; // set system date
		CALLBACK_RunRealInt(0x21);
		if (reg_al == 0xff) WriteOut(MSG_Get("SHELL_CMD_DATE_ERROR"));
		return;
	}
	// display the current date
	reg_ah=0x2a; // get system date
	CALLBACK_RunRealInt(0x21);

	const char* datestring = MSG_Get("SHELL_CMD_DATE_DAYS");
	Bit32u length;
	char day[6] = {0};
	if (sscanf(datestring,"%u",&length) && (length<5) && (strlen(datestring) == (length*7+1))) {
		// date string appears valid
		for (Bit32u i = 0; i < length; i++) day[i] = datestring[reg_al*length+1+i];
	}
	bool dateonly = ScanCMDBool(args,"T");
	if (!dateonly) WriteOut(MSG_Get("SHELL_CMD_DATE_NOW"));

	const char* formatstring = MSG_Get("SHELL_CMD_DATE_FORMAT");
	if (strlen(formatstring)!=5) return;
	char buffer[15] = {0};
	Bitu bufferptr=0;
	for (Bitu i = 0; i < 5; i++) {
		if (i == 1 || i == 3) {
			buffer[bufferptr] = formatstring[i];
			bufferptr++;
		} else {
			if (formatstring[i] == 'M') bufferptr += sprintf(buffer+bufferptr,"%02u",(Bit8u) reg_dh);
			if (formatstring[i] == 'D') bufferptr += sprintf(buffer+bufferptr,"%02u",(Bit8u) reg_dl);
			if (formatstring[i] == 'Y') bufferptr += sprintf(buffer+bufferptr,"%04u",(Bit16u) reg_cx);
		}
	}
	WriteOut("%s %s\n",day, buffer);
	if (!dateonly) WriteOut(MSG_Get("SHELL_CMD_DATE_SETHLP"));
};

void DOS_Shell::CMD_TIME(char * args) {
	HELP("TIME");
	if (ScanCMDBool(args,"H")) {
		// synchronize time with host parameter
		time_t curtime;
		struct tm *loctime;
		curtime = time (NULL);
		loctime = localtime (&curtime);

		//reg_cx = loctime->;
		//reg_dh = loctime->;
		//reg_dl = loctime->;

		// reg_ah=0x2d; // set system time TODO
		// CALLBACK_RunRealInt(0x21);

		Bit32u ticks=(Bit32u)(((double)(loctime->tm_hour*3600+
										loctime->tm_min*60+
										loctime->tm_sec))*18.206481481);
		mem_writed(BIOS_TIMER,ticks);
		return;
	}
	bool timeonly = ScanCMDBool(args,"T");

	reg_ah=0x2c; // get system time
	CALLBACK_RunRealInt(0x21);
/*
		reg_dl= // 1/100 seconds
		reg_dh= // seconds
		reg_cl= // minutes
		reg_ch= // hours
*/
	if (timeonly) {
		WriteOut("%2u:%02u\n",reg_ch,reg_cl);
	} else {
		WriteOut(MSG_Get("SHELL_CMD_TIME_NOW"));
		WriteOut("%2u:%02u:%02u,%02u\n",reg_ch,reg_cl,reg_dh,reg_dl);
	}
};

void DOS_Shell::CMD_SUBST(char * args) {
/* If more that one type can be substed think of something else
 * E.g. make basedir member dos_drive instead of localdrive
 */
	HELP("SUBST");
	localDrive* ldp=0;
	char mountstring[DOS_PATHLENGTH+CROSS_LEN+20];
	char temp_str[2] = { 0,0 };
	try {
		safe_strcpy(mountstring, "MOUNT ");
		StripSpaces(args);
		std::string arg;
		CommandLine command(0,args);

		if (command.GetCount() != 2) throw 0 ;

		command.FindCommand(1,arg);
		if ( (arg.size() > 1) && arg[1] !=':')  throw(0);
		temp_str[0]=(char)toupper(args[0]);
		command.FindCommand(2,arg);
		if ((arg == "/D") || (arg == "/d")) {
			if (!Drives[temp_str[0]-'A'] ) throw 1; //targetdrive not in use
			strcat(mountstring,"-u ");
			strcat(mountstring,temp_str);
			this->ParseLine(mountstring);
			return;
		}
		if (Drives[temp_str[0]-'A'] ) throw 0; //targetdrive in use
		strcat(mountstring,temp_str);
		strcat(mountstring," ");

		Bit8u drive;char fulldir[LFN_NAMELENGTH+2];
		if (!DOS_MakeName(const_cast<char*>(arg.c_str()),fulldir,&drive)) throw 0;

		if ( ( ldp=dynamic_cast<localDrive*>(Drives[drive])) == 0 ) throw 0;
		char newname[CROSS_LEN];
		safe_strcpy(newname, ldp->getBasedir());
		strcat(newname,fulldir);
		CROSS_FILENAME(newname);
		ldp->dirCache.ExpandName(newname);
		strcat(mountstring,"\"");
		strcat(mountstring, newname);
		strcat(mountstring,"\"");
		this->ParseLine(mountstring);
	}
	catch(int a){
		if (a == 0) {
			WriteOut(MSG_Get("SHELL_CMD_SUBST_FAILURE"));
		} else {
		       	WriteOut(MSG_Get("SHELL_CMD_SUBST_NO_REMOVE"));
		}
		return;
	}
	catch(...) {		//dynamic cast failed =>so no localdrive
		WriteOut(MSG_Get("SHELL_CMD_SUBST_FAILURE"));
		return;
	}

	return;
}

void DOS_Shell::CMD_LOADHIGH(char *args){
	HELP("LOADHIGH");
	Bit16u umb_start=dos_infoblock.GetStartOfUMBChain();
	Bit8u umb_flag=dos_infoblock.GetUMBChainState();
	Bit8u old_memstrat=(Bit8u)(DOS_GetMemAllocStrategy()&0xff);
	if (umb_start == 0x9fff) {
		if ((umb_flag&1) == 0) DOS_LinkUMBsToMemChain(1);
		DOS_SetMemAllocStrategy(0x80);	// search in UMBs first
		this->ParseLine(args);
		Bit8u current_umb_flag=dos_infoblock.GetUMBChainState();
		if ((current_umb_flag&1)!=(umb_flag&1)) DOS_LinkUMBsToMemChain(umb_flag);
		DOS_SetMemAllocStrategy(old_memstrat);	// restore strategy
	} else this->ParseLine(args);
}

void DOS_Shell::CMD_CHOICE(char * args){
	HELP("CHOICE");
	static char defchoice[3] = {'y','n',0};
	char *rem = NULL, *ptr;
	bool optN = false;
	bool optS = false;
	if (args) {
		optN = ScanCMDBool(args,"N");
		optS = ScanCMDBool(args,"S"); //Case-sensitive matching
		ScanCMDBool(args,"T"); //Default Choice after timeout
		char *last = strchr(args,0);
		StripSpaces(args);
		rem = ScanCMDRemain(args);

		if (rem && *rem && (tolower(rem[1]) != 'c')) {
			WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
			return;
		}
		if (args == rem) {
			assert(args);
			if (rem != nullptr) {
				args = strchr(rem, '\0') + 1;
			}
		}
		if (rem) rem += 2;
		if (rem && rem[0] == ':') rem++; /* optional : after /c */
		if (args > last) args = NULL;
	}
	if (!rem || !*rem) rem = defchoice; /* No choices specified use YN */
	ptr = rem;
	Bit8u c;
	if (!optS) while ((c = *ptr)) *ptr++ = (char)toupper(c); /* When in no case-sensitive mode. make everything upcase */
	if (args && *args ) {
		StripSpaces(args);
		size_t argslen = strlen(args);
		if (argslen > 1 && args[0] == '"' && args[argslen-1] == '"') {
			args[argslen-1] = 0; //Remove quotes
			args++;
		}
		WriteOut(args);
	}
	/* Show question prompt of the form [a,b]? where a b are the choice values */
	if (!optN) {
		if (args && *args) WriteOut(" ");
		WriteOut("[");
		size_t len = strlen(rem);
		for (size_t t = 1; t < len; t++) {
			WriteOut("%c,",rem[t-1]);
		}
		WriteOut("%c]?",rem[len-1]);
	}

	Bit16u n=1;
	do {
		DOS_ReadFile (STDIN,&c,&n);
	} while (!c || !(ptr = strchr(rem,(optS?c:toupper(c)))));
	c = optS?c:(Bit8u)toupper(c);
	DOS_WriteFile(STDOUT, &c, &n);
	WriteOut_NoParsing("\n");
	dos.return_code = (Bit8u)(ptr-rem+1);
}

void DOS_Shell::CMD_ATTRIB(char *args){
	HELP("ATTRIB");
	// No-Op for now.
}

void DOS_Shell::CMD_PATH(char *args){
	HELP("PATH");
	if (args && strlen(args)) {
		char set_path[DOS_PATHLENGTH + CROSS_LEN + 20] = {0};
		while (args && *args && (*args == '='|| *args == ' '))
			args++;
		snprintf(set_path, sizeof(set_path), "set PATH=%s", args);
		this->ParseLine(set_path);
		return;
	} else {
		std::string line;
		if (GetEnvStr("PATH", line))
			WriteOut("%s\n", line.c_str());
		else
			WriteOut("PATH=(null)\n");
	}
}

void DOS_Shell::CMD_VER(char *args) {
	HELP("VER");
	if (args && strlen(args)) {
		char* word = StripWord(args);
		if(strcasecmp(word,"set")) return;
		word = StripWord(args);
		if (!*args && !*word) { //Reset
			dos.version.major = 5;
			dos.version.minor = 0;
		} else if (*args == 0 && *word && (strchr(word,'.') != 0)) { //Allow: ver set 5.1
			const char * p = strchr(word,'.');
			dos.version.major = (Bit8u)(atoi(word));
			dos.version.minor = (Bit8u)(strlen(p+1)==1&&*(p+1)>'0'&&*(p+1)<='9'?atoi(p+1)*10:atoi(p+1));
		} else { //Official syntax: ver set 5 2
			dos.version.major = (Bit8u)(atoi(word));
			dos.version.minor = (Bit8u)(atoi(args));
		}
		if (enablelfn != -2) uselfn = enablelfn==1 || (enablelfn == -1 && dos.version.major>6);
	} else WriteOut(MSG_Get("SHELL_CMD_VER_VER"),VERSION,dos.version.major,dos.version.minor);
}
