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
 */

#ifndef DOSBOX_SETUP_H
#define DOSBOX_SETUP_H

#include <cstdio>
#include <list>
#include <string>
#include <vector>

#include "support.h"

class Hex {
private:
	int _hex;
public:
	Hex(int in):_hex(in) { };
	Hex():_hex(0) { };
	bool operator==(Hex const& other) {return _hex == other._hex;}
	operator int () const { return _hex; }
};

class Value {
/* 
 * Multitype storage container that is aware of the currently stored type in it.
 * Value st = "hello";
 * Value in = 1;
 * st = 12 //Exception
 * in = 12 //works
 */
private:
	Hex _hex;
	bool _bool;
	int _int;
	std::string* _string;
	double _double;
public:
	class WrongType { }; // Conversion error class
	enum Etype { V_NONE, V_HEX, V_BOOL, V_INT, V_STRING, V_DOUBLE,V_CURRENT} type;

	/* Constructors */
	Value()                      :_hex(0), _bool(false),_int(0), _string(0),                  _double(0), type(V_NONE)   { };
	Value(Hex in)                :_hex(in),_bool(false),_int(0), _string(0),                  _double(0), type(V_HEX)    { };
	Value(int in)                :_hex(0), _bool(false),_int(in),_string(0),                  _double(0), type(V_INT)    { };
	Value(bool in)               :_hex(0), _bool(in)   ,_int(0), _string(0),                  _double(0), type(V_BOOL)   { };
	Value(double in)             :_hex(0), _bool(false),_int(0), _string(0),                  _double(in),type(V_DOUBLE) { };
	Value(std::string const& in) :_hex(0), _bool(false),_int(0), _string(new std::string(in)),_double(0), type(V_STRING) { };
	Value(char const * const in) :_hex(0), _bool(false),_int(0), _string(new std::string(in)),_double(0), type(V_STRING) { };
	Value(Value const& in):_string(0) {plaincopy(in);}
	~Value() { destroy();};
	Value(std::string const& in,Etype _t) :_hex(0),_bool(false),_int(0),_string(0),_double(0),type(V_NONE) {SetValue(in,_t);}

	/* Assigment operators */
	Value& operator= (Hex in)                 { return copy(Value(in));}
	Value& operator= (int in)                 { return copy(Value(in));}
	Value& operator= (bool in)                { return copy(Value(in));}
	Value& operator= (double in)              { return copy(Value(in));}
	Value& operator= (std::string const& in)  { return copy(Value(in));}
	Value& operator= (char const * const in)  { return copy(Value(in));}
	Value& operator= (Value const& in)        { return copy(Value(in));}

	bool operator== (Value const & other) const;
	operator bool () const;
	operator Hex () const;
	operator int () const;
	operator double () const;
	operator char const* () const;
	bool SetValue(std::string const& in,Etype _type = V_CURRENT);
	std::string ToString() const;

private:
	void destroy() throw();
	Value& copy(Value const& in);
	void plaincopy(Value const& in) throw();
	bool set_hex(std::string const& in);
	bool set_int(std::string const&in);
	bool set_bool(std::string const& in);
	void set_string(std::string const& in);
	bool set_double(std::string const& in);
};

class Property {
public:
	struct Changeable {
		enum Value { Always, WhenIdle, OnlyAtStart, Deprecated };
	};

	const std::string propname;

	Property(const std::string &name, Changeable::Value when)
		: propname(name),
		  value(),
		  suggested_values{},
		  default_value(),
		  change(when)
	{
		assertm(!name.empty(), "Property name can't be empty.");
	}

	virtual ~Property() = default;

	void Set_values(const char * const * in);
	void Set_values(const std::vector<std::string> &in);
	void Set_help(std::string const& str);

	const char* GetHelp() const;

	virtual	bool SetValue(std::string const& str)=0;
	Value const& GetValue() const { return value;}
	Value const& Get_Default_Value() const { return default_value; }
	//CheckValue returns true, if value is in suggested_values;
	//Type specific properties are encouraged to override this and check for type
	//specific features.
	virtual bool CheckValue(Value const& in, bool warn);

	Changeable::Value GetChange() const { return change; }
	bool IsDeprecated() const { return (change == Changeable::Value::Deprecated); }

	virtual const std::vector<Value>& GetValues() const;
	Value::Etype Get_type(){return default_value.type;}

protected:
	//Set interval value to in or default if in is invalid. force always sets the value.
	//Can be overriden to set a different value if invalid.
	virtual bool SetVal(Value const& in, bool forced,bool warn=true) {
		if(forced || CheckValue(in,warn)) {
			value = in; return true;
		} else {
			value = default_value; return false;
		}
	}
	Value value;
	std::vector<Value> suggested_values;
	typedef std::vector<Value>::const_iterator const_iter;
	Value default_value;
	const Changeable::Value change;
};

class Prop_int:public Property {
public:
	Prop_int(std::string const& _propname,Changeable::Value when, int _value)
		:Property(_propname,when) {
		default_value = value = _value;
		min = max = -1;
	}
	Prop_int(std::string const&  _propname,Changeable::Value when, int _min,int _max,int _value)
		:Property(_propname,when) {
		default_value = value = _value;
		min = _min;
		max = _max;
	}
	int getMin() { return min;}
	int getMax() { return max;}
	void SetMinMax(Value const& _min,Value const& _max) {this->min = _min; this->max=_max;}
	bool SetValue(std::string const& in);
	~Prop_int(){ }
	virtual bool CheckValue(Value const& in, bool warn);
	// Override SetVal, so it takes min,max in account when there are no suggested values
	virtual bool SetVal(Value const& in, bool forced,bool warn=true);

private:
	Value min,max;
};

class Prop_double:public Property {
public:
	Prop_double(std::string const & _propname, Changeable::Value when, double _value)
		:Property(_propname,when){
		default_value = value = _value;
	}
	bool SetValue(std::string const& input);
	~Prop_double(){ }
};

class Prop_bool:public Property {
public:
	Prop_bool(std::string const& _propname, Changeable::Value when, bool _value)
		:Property(_propname,when) {
		default_value = value = _value;
	}
	bool SetValue(std::string const& in);
	~Prop_bool(){ }
};

class Prop_string:public Property{
public:
	Prop_string(std::string const& _propname, Changeable::Value when, char const * const _value)
		:Property(_propname,when) {
		default_value = value = _value;
	}
	bool SetValue(std::string const& in);
	virtual bool CheckValue(Value const& in, bool warn);
	~Prop_string(){ }
};
class Prop_path:public Prop_string{
public:
	std::string realpath;
	Prop_path(std::string const& _propname, Changeable::Value when, char const * const _value)
		:Prop_string(_propname,when,_value) {
		default_value = value = _value;
		realpath = _value;
	}
	bool SetValue(std::string const& in);
	~Prop_path(){ }
};

class Prop_hex:public Property {
public:
	Prop_hex(std::string const& _propname, Changeable::Value when, Hex _value)
		:Property(_propname,when) {
		default_value = value = _value;
	}
	bool SetValue(std::string const& in);
	~Prop_hex(){ }
};

#define NO_SUCH_PROPERTY "PROP_NOT_EXIST"

class Section {
private:
	typedef void (*SectionFunction)(Section*);

	/* Wrapper class around startup and shutdown functions. the variable
	 * canchange indicates it can be called on configuration changes */
	struct Function_wrapper {
		SectionFunction function;
		bool canchange;

		Function_wrapper(SectionFunction const fn, bool ch)
		        : function(fn),
		          canchange(ch)
		{}
	};

	std::list<Function_wrapper> initfunctions = {};
	std::list<Function_wrapper> destroyfunctions = {};
	std::string sectionname;
public:
	Section(const std::string &name) : sectionname(name) {}

	virtual ~Section() = default; // Children must call executedestroy!

	void AddInitFunction(SectionFunction func,bool canchange=false);
	void AddDestroyFunction(SectionFunction func,bool canchange=false);
	void ExecuteInit(bool initall=true);
	void ExecuteDestroy(bool destroyall=true);
	const char* GetName() const {return sectionname.c_str();}

	virtual std::string GetPropValue(const std::string &property) const = 0;
	virtual bool HandleInputline(const std::string &line) = 0;
	virtual void PrintData(FILE *outfile) const = 0;
};

class Prop_multival;
class Prop_multival_remain;
class Section_prop:public Section {
private:
	std::list<Property*> properties;
	typedef std::list<Property*>::iterator it;
	typedef std::list<Property*>::const_iterator const_it;

public:
	Section_prop(std::string const&  _sectionname):Section(_sectionname){}
	Prop_int* Add_int(std::string const& _propname, Property::Changeable::Value when, int _value=0);
	Prop_string* Add_string(std::string const& _propname, Property::Changeable::Value when, char const * const _value=NULL);
	Prop_path* Add_path(std::string const& _propname, Property::Changeable::Value when, char const * const _value=NULL);
	Prop_bool*  Add_bool(std::string const& _propname, Property::Changeable::Value when, bool _value=false);
	Prop_hex* Add_hex(std::string const& _propname, Property::Changeable::Value when, Hex _value=0);
//	void Add_double(char const * const _propname, double _value=0.0);
	Prop_multival *Add_multi(std::string const& _propname, Property::Changeable::Value when,std::string const& sep);
	Prop_multival_remain *Add_multiremain(std::string const& _propname, Property::Changeable::Value when,std::string const& sep);

	Property* Get_prop(int index);
	int Get_int(std::string const& _propname) const;
	const char* Get_string(std::string const& _propname) const;
	bool Get_bool(std::string const& _propname) const;
	Hex Get_hex(std::string const& _propname) const;
	double Get_double(std::string const& _propname) const;
	Prop_path* Get_path(std::string const& _propname) const;
	Prop_multival* Get_multival(std::string const& _propname) const;
	Prop_multival_remain* Get_multivalremain(std::string const& _propname) const;
	bool HandleInputline(std::string const& gegevens);
	void PrintData(FILE* outfile) const;
	virtual std::string GetPropValue(std::string const& _property) const;
	//ExecuteDestroy should be here else the destroy functions use destroyed properties
	virtual ~Section_prop();
};

class Prop_multival:public Property{
protected:
	Section_prop* section;
	std::string separator;
	void make_default_value();
public:
	Prop_multival(std::string const& _propname, Changeable::Value when,std::string const& sep):Property(_propname,when), section(new Section_prop("")),separator(sep) {
		default_value = value = "";
	}
	Section_prop *GetSection() { return section; }
	const Section_prop *GetSection() const { return section; }
	virtual bool SetValue(std::string const& input);
	virtual const std::vector<Value>& GetValues() const;
	~Prop_multival() { delete section; }
}; //value bevat totale string. setvalue zet elk van de sub properties en checked die.

class Prop_multival_remain:public Prop_multival{
public:
	Prop_multival_remain(std::string const& _propname, Changeable::Value when,std::string const& sep):Prop_multival(_propname,when,sep){ }

	virtual bool SetValue(std::string const& input);
};

class Section_line : public Section {
public:
	Section_line(std::string const &name) : Section(name), data() {}

	~Section_line() override { ExecuteDestroy(true); }

	std::string GetPropValue(const std::string &property) const override;
	bool HandleInputline(const std::string &line) override;
	void PrintData(FILE *outfile) const override;

	std::string data;
};

/* Base for all hardware and software "devices" */
class Module_base {
protected:
	Section *m_configuration;

public:
	Module_base(Section *conf_section) : m_configuration(conf_section) {}

	Module_base(const Module_base &) = delete; // prevent copying
	Module_base &operator=(const Module_base &) = delete; // prevent assignment

	virtual ~Module_base() = default;

	virtual bool Change_Config(Section * /*newconfig*/) { return false; }
};

#endif
