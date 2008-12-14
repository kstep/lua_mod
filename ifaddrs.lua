package.loadlib("./lifaddrs.so", "luaopen_ifaddrs")()

iface = ifaddrs.init();
print(iface["wpi0"]);
print(iface["fxp0"]);
print(iface["fxp0"].name);

ifanames = { [2] = "AF_INET", [18] = "AF_LINK", [28] = "AF_INET6" }
--[[
AF_LINK => sockaddr_dl
AF_INET => sockaddr_in
AF_INET6 => sockaddr_in6
]]

for ifa in iface:each() do
	print(ifa.name .. ": " .. ifanames[ifa.family])
end
