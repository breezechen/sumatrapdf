#!/usr/bin/env python

# TODO: generate default data as serialized block
# TODO: add
class Struct(object):
	def __init__(self, name, fields):
		self.name = name
		self.fields = fields

g_struct_type = type(Struct(None, None))

class Field(object):
	types = ["bool", "color", "int", "uint16"]

	def __init__(self, name, typ, def_val):
		assert typ in Field.types or type(typ) == g_struct_type
		self.name = name
		self.typ = typ
		self.def_val = def_val

# TODO: will we need ability to over-ride defaults?
paddingStruct = Struct("ConfigPadding", [
	Field("top", 	"uint16", 2),
	Field("bottom", "uint16", 2),
	Field("left", 	"uint16", 4),
	Field("right", 	"uint16", 4),
	Field("spaceX", "uint16", 4),
	Field("spaceY", "uint16", 4),
])

forwardSearchStruct = Struct("ForwardSearchConfig", [
	Field("highlightOffset", "int", 0),
	Field("highlightWidth", "int", 15),
	Field("highlightColor", "color", 0x6581FF),
	Field("highlightPermanent", "int", 0),
])

configStruct = Struct("SumatraConfig", [
	Field("traditionalEbookUI", "bool", False),
	Field("logoColor", "color", 0xFFF200),
	Field("escToExit", "bool", False),
	Field("pagePadding", paddingStruct, None),
	Field("forwardSearch", forwardSearchStruct, None),
])

def typ_to_c_type(typ):
	if typ == "color": return "uint32_t"
	if type(typ) == g_struct_type: return typ.name + " *"
	return typ

c_hdr = """// DON'T MODIFY MANUALLY !!!!
// auto-generated by scripts/gen_config.py !!!!

"""

def gen_c_for_struct(struct):
	res = ["struct %s {" % struct.name]
	for field in struct.fields:
		typ = typ_to_c_type(field.typ)
		s = "    %-24s %s;" % (typ, field.name)
		res.append(s)
	res.append("};")
	return "\n".join(res) + "\n"

def gen_c():
	res = [gen_c_for_struct(s) for s in [paddingStruct, forwardSearchStruct, configStruct]]
	return "\n".join(res)

def main():
	s = gen_c()
	print(c_hdr + s)

if __name__ == "__main__":
	main()
