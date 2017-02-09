f = open('large_file.txt','r+')
f.seek(45)
f.write("hello")
f.close()
