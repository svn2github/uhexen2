/*
	cvar.c
	dynamic variable tracking

	$Id: cvar.c,v 1.26 2007-04-10 17:53:05 sezero Exp $
*/

#include "quakedef.h"

cvar_t	*cvar_vars;
static	char	*cvar_null_string = "";

/*
============
Cvar_FindVar
============
*/
cvar_t *Cvar_FindVar (const char *var_name)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
	{
		if (!strcmp (var_name, var->name))
			return var;
	}

	return NULL;
}

/*
============
Cvar_LockVars

used for preventing the early-read cvar values to get over-
written by the actual final read of config.cfg (which will
be the case when commandline overrides were used):  mark
them as locked until Host_Init() completely finishes its job.
this is a temporary solution until we adopt a better init
sequence employing the +set arguments like those in quake2/3.
the num_vars argument must be the exact number of strings in the
array, otherwise I have nothing against going out of bounds.
============
*/
void Cvar_LockVars (const char **varnames, int num_vars)
{
	cvar_t	*var;
	int		i;

	for (i = 0; i < num_vars && varnames[i]; i++)
	{
		var = Cvar_FindVar (varnames[i]);
		if (var)
			var->flags |= CVAR_LOCKED;
	}
}

void Cvar_LockVar (const char *var_name)
{
	cvar_t	*var = Cvar_FindVar (var_name);
	if (var)
		var->flags |= CVAR_LOCKED;
}

void Cvar_UnlockVar (const char *var_name)
{
	cvar_t	*var = Cvar_FindVar (var_name);
	if (var)
		var->flags &= ~CVAR_LOCKED;
}

void Cvar_UnlockAll (void)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
	{
		var->flags &= ~CVAR_LOCKED;
	}
}

/*
============
Cvar_VariableValue
============
*/
float	Cvar_VariableValue (const char *var_name)
{
	cvar_t	*var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return 0;
	return atof (var->string);
}


/*
============
Cvar_VariableString
============
*/
char *Cvar_VariableString (const char *var_name)
{
	cvar_t *var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return cvar_null_string;
	return var->string;
}


/*
============
Cvar_Set
============
*/
void Cvar_Set (const char *var_name, const char *value)
{
	cvar_t	*var;
	qboolean changed;

	var = Cvar_FindVar (var_name);
	if (!var)
	{	// there is an error in C code if this happens
		Con_Printf ("%s: variable %s not found\n", __FUNCTION__, var_name);
		return;
	}

	if ( var->flags & (CVAR_ROM|CVAR_LOCKED) )
		return;	// cvar is marked read-only or locked temporarily

	changed = strcmp(var->string, value);

	Z_Free (var->string);	// free the old value string

	var->string = Z_Malloc (strlen(value)+1, Z_MAINZONE);
	strcpy (var->string, value);
	var->value = atof (var->string);
	if ((var->flags & CVAR_NOTIFY) && changed)
	{
		if (sv.active)
			SV_BroadcastPrintf ("\"%s\" changed to \"%s\"\n", var->name, var->string);
	}

	// Don't allow deathmatch and coop at the same time
	if ( !strcmp(var->name, deathmatch.name) )
	{
		if (var->value != 0)
			Cvar_Set("coop", "0");
	}
	else if ( !strcmp(var->name, coop.name) )
	{
		if (var->value != 0)
			Cvar_Set("deathmatch", "0");
	}
}

/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue (const char *var_name, const float value)
{
	char	val[32];

	snprintf (val, sizeof(val), "%g", value);
	Cvar_Set (var_name, val);
}


/*
============
Cvar_RegisterVariable

Adds a freestanding variable to the variable list.
============
*/
void Cvar_RegisterVariable (cvar_t *variable)
{
	char	value[512];
	qboolean	set_rom = false;

// first check to see if it has already been defined
	if (Cvar_FindVar (variable->name))
	{
		Con_Printf ("Can't register variable %s, already defined\n", variable->name);
		return;
	}

// check for overlap with a command
	if (Cmd_Exists (variable->name))
	{
		Con_Printf ("%s: %s is a command\n", __FUNCTION__, variable->name);
		return;
	}

// link the variable in
	variable->next = cvar_vars;
	cvar_vars = variable;

// copy the value off, because future sets will Z_Free it
	strncpy (value, variable->string, 511);
	value[511] = '\0';
	variable->string = Z_Malloc (1, Z_MAINZONE);

// set it through the function to be consistant
	set_rom = (variable->flags & CVAR_ROM);
	variable->flags &= ~CVAR_ROM;
	Cvar_Set (variable->name, value);
	if (set_rom)
		variable->flags |= CVAR_ROM;
}

/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
qboolean	Cvar_Command (void)
{
	cvar_t			*v;

// check variables
	v = Cvar_FindVar (Cmd_Argv(0));
	if (!v)
		return false;

// perform a variable print or set
	if (Cmd_Argc() == 1)
	{
		Con_Printf ("\"%s\" is \"%s\"\n", v->name, v->string);
		return true;
	}

	Cvar_Set (v->name, Cmd_Argv(1));
	return true;
}


/*
============
Cvar_WriteVariables

Writes lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void Cvar_WriteVariables (FILE *f)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
	{
		if (var->flags & CVAR_ARCHIVE)
			fprintf (f, "%s \"%s\"\n", var->name, var->string);
	}
}

