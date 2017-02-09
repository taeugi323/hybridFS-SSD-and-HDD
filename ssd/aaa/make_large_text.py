import random

f = open("large_file.txt",'w')

input_str = ""

want_to_make = raw_input("input size to generate(e.g. 1G, 10M, etc) : ")

unit = 1

if want_to_make[-1].upper() == 'G':
    unit = 2**20
if want_to_make[-1].upper() == 'M':
    unit = 2**10

unit = unit * int(want_to_make[0:-1])

# input_str is 1KB string
for j in range(64):
    for i in range(15):
        input_str += str(unichr(random.randrange(26)+97))
    input_str += "\n"

for i in range(unit):
    f.write(input_str)

f.close()
