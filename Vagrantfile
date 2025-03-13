#
# *** Red Green Cluster ***


VAGRANTFILE_API_VERSION = "2"
# set docker as the default provider
ENV['VAGRANT_DEFAULT_PROVIDER'] = 'docker'
# disable parallellism so that the containers come up in order
ENV['VAGRANT_NO_PARALLEL'] = "1"
ENV['FORWARD_DOCKER_PORTS'] = "1"
# minor hack enabling to run the image and configuration trigger just once
ENV['VAGRANT_EXPERIMENTAL']="typed_triggers"

unless Vagrant.has_plugin?("vagrant-docker-compose")
  system("vagrant plugin install vagrant-docker-compose")
  puts "Dependencies installed, please try the command again."
  exit
end

# Names of Docker images built:
NODE_IMAGE  = "ds/clusternode:0.1"

# Node definitions
          
NODES  = { :nameprefix => "node-",  # backend nodes get names: backend-1, backend-2, etc.
              :subnet => "10.0.1.",
              :ip_offset => 100,  # backend nodes get IP addresses: 10.0.1.101, .102, .103, etc
              :image => NODE_IMAGE,
              :port => 5000 }
# Number of backend to start:
NODES_COUNT = 10

# Common configuration
Vagrant.configure(VAGRANTFILE_API_VERSION) do |config|
  #config.vm.synced_folder '.', '/vagrant', disabled: true

  # Before the 'vagrant up' command is started, build docker images:
  config.trigger.before :up, type: :command do |trigger|
    trigger.name = "Build docker images and configuration files"
    trigger.ruby do |env, machine|
      # --- start of Ruby script ---
      # Build load balancer configuration file:
      # Build image for backend nodes:
      puts "Building backend node image:"
      `docker build node -t "#{NODE_IMAGE}"`
      # Build image for the frontend node:
    
      # --- end of Ruby script ---
    end
  end

  #config.vm.synced_folder ".", "/vagrant", type: "rsync", rsync__exclude: ".*/"
  config.ssh.insert_key = false

  # Definition of N backends
  (1..NODES_COUNT).each do |i|
    node_ip_addr = "#{NODES[:subnet]}#{NODES[:ip_offset] + i}"
    node_name = "#{NODES[:nameprefix]}#{i}"
    # Definition of NODE
    #config.vm.synced_folder '.', '/vagrant', disabled: true
    config.vm.define node_name do |s|
      s.vm.network "private_network", ip: node_ip_addr
      s.vm.hostname = node_name
      s.vm.provider "docker" do |d|
        d.build_dir = "node"
        d.build_args = ["-t", "#{NODES[:image]}"]
        d.name = node_name
        d.has_ssh = true
        # Forward port for the first backend node
        d.ports = ["8080:8080"] if i == 1
        #config.vm.network "forwarded_port", guest: 8080, host: 4569, host_ip: "127.0.0.1", protocol: "tcp" if i == 1
      end
      s.vm.post_up_message = "Node #{node_name} up and running. You can access the node with 'vagrant ssh #{node_name}'"
    end
  end
end

# EOF
