
package.loadlib("./test.so", "luaopen_test")()

for i = 1,10 do
	local to = testobj.testobj()
	print(to)
end

