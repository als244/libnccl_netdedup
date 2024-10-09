import torch
import torch.distributed as dist
import os
import sys
import time


SEED = 0


def main(world_size, rank):
        
        tcp_str = "tcp://" + os.environ["MASTER_ADDR"] + ":" + os.environ["MASTER_PORT"]
        
        print(f"Joining distributed process group as rank {rank}...!")
	
        dist.init_process_group(init_method=tcp_str, backend="nccl", rank=rank, world_size=world_size)

        print(f"Finished joining distributed process group!")

        num_els = [4096, 8192]

        times = {}

        torch.manual_seed(SEED)
        
        for d in num_els:
                print(f"Going to start timing simple send/recv for data size (bytes): {d * 4}. Ensuring barrier op first")
                tensor = torch.randn(d, dtype=torch.float32, device="cuda:0")
                dist.barrier()
                start = time.time_ns()
                if rank == 1:
                        dist.recv(tensor, src=0)
                else:
                        dist.send(tensor, dst=1)
                dist.barrier()
                stop = time.time_ns()
                elapsed_ns = stop - start

                if rank == 0:
                    print("Sent tensor:\n")
                else:
                    print("Received tensor:\n")

                print(tensor)
                print("\n\n\n")

		
                num_bits = (d * 4) * 8
                throughput_gb_sec = num_bits / elapsed_ns
                print(f"\n\nSimple send over {d} Floats:\n\tWorld Size: {world_size}\n\tElapsed Time (ns): {elapsed_ns}\n\tThroughput Gb/sec: {throughput_gb_sec}\n\n")
                times[d] = elapsed_ns
                
                del tensor
                tensor = None


if __name__ == "__main__":
        world_size = int(sys.argv[1])
        rank = int(sys.argv[2])
        main(world_size, rank)
