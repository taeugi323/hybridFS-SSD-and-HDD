f = open('large_file.txt','r+')
f.seek(100)
f.write("non_hello")
f.close()
