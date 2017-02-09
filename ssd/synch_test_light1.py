f = open('large_file.txt','r+')
f.seek(4)
f.write("hello")
f.close()
