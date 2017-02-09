f = open('large_file.txt','r+')
f.seek(70)
f.write("hello")
f.close()
