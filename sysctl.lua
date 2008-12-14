package.loadlib("./lsysctl.so", "luaopen_sysctl")()

function print_sysctl(name)
	print(name .. ": ")
	print(sysctl.get(name))
	print()
end

function print_tbl(name, tbl,level)
	if not level then level = 0 end
	local indchar = "    ";
	local indent = indchar:rep(level)
	print(indent .. name .. " = {")
	for k,v in pairs(tbl) do
		if type(v) == "table" then
			print_tbl(k,v,level + 1)
		else
			print(indent..indchar..k.." = "..tostring(v))
		end
	end
	print(indent.."}")
end
--[===[
print("\n=== direct get node values ===")
print_sysctl("hw.acpi.thermal.tz0._CRT")
print_sysctl("dev.cpu.0.freq_levels")
print_sysctl("dev.cpu.0.freq")
print_sysctl("dev.cpu.0.temperature")
print_sysctl("dev.cpu.0.%desc")

print("\n=== simple get node ===")
node = sysctl.node("hw.acpi.thermal.tz0._CRT");
print(node);
print("\nvia method:")
print(node:get());
print("\nvia property:")
print(node.value);

print("\n=== get next node ===")

node = sysctl.node("dev.acpi")
print(node);
print(node:get());
print("\nnext via method:")
print(node:next());
print("\n2nd next cascaded method call:");
print(node:next():next());

print("\nnext via iterator:")
f,s,v = sysctl.each(node);
print("iterator:",f)
print("state:",s)
print("firstvalue:",v);
print("call iterator:",f(s,v));


print("\n=== loop ===");
for n in sysctl.each("dev.cpu.0") do
	print(n)
end

print("\n=== struct node get ===")
node = sysctl.node("kern.boottime")
print(node)
print("via method:")
print(node:get())
print("via property:")
print(node.value)

print("\n=== get node properties ===")
print(node)
print("name: ", node.name);
print("desc: ", node.desc);
print("type: ", node.type);
print("struct: ", node.struct);
print("format: ", node.format);
print("readonly: ", node.readonly);

print("\n=== get multi value int ===")
node = sysctl.node("kern.cp_time")
print(node)
print("via method:")
print(node:get())
print("via property:")
print(node.value)

--[[
print()
node = sysctl.node("net.inet.ip.stats")
print(node)

for k,n in pairs(node.value) do
	print(k, n)
end
]]

print("\n=== get nodes directly by MIB ids via node:node method (ifmib example) ===")
-- this is an entry point to ifmib data (see man ifmib for details)
node = sysctl.node("net.link.generic.ifdata")
print(node)

-- enum all interfaces
for i = 1,sysctl.get("net.link.generic.system.ifcount") do
	-- get node, describing interface
	n = node:node(i,1)
	t = n:get()
	print_tbl(tostring(n),t)
	-- get & print interface's name
	--print(node:node(i,3):get())
	-- example of some interesting data fetching
	-- we use modulo operator to get last bit of flags
	-- b/c there's no binary and operator in Lua
	--print("flags:",t["data"]["link_state"],"up:",(t["flags"] % 2))
	print()
end
node = sysctl.node("vm.vmtotal")
print_tbl(tostring(node), node:get())
node = sysctl.node("vm.loadavg")
print_tbl(tostring(node), { node:get() })
]===]

print("\n=== list all nodes in system ===")
for n in sysctl.each() do
	print(n,n.desc)
end
--[[
node = sysctl.node("vm.swap_info")
print(node:node(0))
print_tbl("swap_info",node:node(0):get())
for n in sysctl.each(node) do print(n) end
if (node:node(1):get()) then print("ok") else print("nope") end
]]
