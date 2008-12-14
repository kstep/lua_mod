
package.loadlib("./lmntinfo.so", "luaopen_mntinfo")()

function humanize_size(size)
	local suffix = { "b", "Kb", "Mb", "Gb", "Tb" }
	local i = 1
	while size > 1024 and i < #suffix do
		size = size / 1024
		i = i + 1
	end
	return string.format("%0.1f %s", size, suffix[i])
end

function print_tbl(tbl)
	for k,v in pairs(tbl) do
		if type(v) == "table" then
			print("=== table " .. k .. " ===")
			print_tbl(v)
			print("=== end table " .. k .. " ===")
		else
			print(k,v)
		end
	end
end

print()
print("===== direct get =====")
root = mntinfo.getstat("/")
print(root)
print_tbl(root)

print()
print("===== loop =====")
for i,s in mntinfo.each() do
	print("=== "..i.." ===")
	print_tbl(s)
	print()
end

print()
rootfs = mntinfo.getstat("/")
print("Total space:", humanize_size(rootfs["blocks"] * rootfs["bsize"]))
print("Avail space:", humanize_size(rootfs["bavail"] * rootfs["bsize"]))
