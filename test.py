import sys
import time
import pywinble

info = pywinble.info()

print(info)

def on_status(err, stat):
    print("advertisement status", err, stat)
    sys.stdout.flush()

# pywinble.advertise("vida:"  + info["BluetoothAddress"], on_status)


print("HERE 1")

sys.stdout.flush()

# provider = pywinble.provide("eab08fe8-e7bd-4982-836e-8ec0839320ed", {
#     "4bb25162-d43e-47c9-ae53-ba8de3e7b536" : { 
#         "flags" : 8,
#         "static" : "abc",
#         "read_protection" : 0,
#         "description" : "test read",
#         "write_protection" : 0,
#         },
#     "ec563d40-6a7c-4cd9-a1b9-c37882d66fa4" : { 
#         "flags" : 4,
#         "description" : "test write",
#         }
#     })

provider = pywinble.provide("eab08fe8-e7bd-4982-836e-8ec0839320ed", {
    "ec563d40-6a7c-4cd9-a1b9-c37882d66fa4" : { 
        "flags" : 4,
        "description" : "test write",
        }
    })

print("HERE 2")

sys.stdout.flush()

print("uuid is", provider.uuid)

sys.stdout.flush()

provider.start()

time.sleep(0.5)

provider.stop()

print("got to end")
