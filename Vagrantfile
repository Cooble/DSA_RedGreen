#
# *** Demo 3
# Create N backend nodes and 1 frontend node working as a load-balancer.
#

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
BACKEND_IMAGE  = "ds/demo-3/backend:0.1"

# Node definitions
          
BACKENDS  = { :nameprefix => "backend-",  # backend nodes get names: backend-1, backend-2, etc.
              :subnet => "10.0.1.",
              :ip_offset => 100,  # backend nodes get IP addresses: 10.0.1.101, .102, .103, etc
              :image => BACKEND_IMAGE,
              :port => 5000 }
# Number of backend to start:
BACKENDS_COUNT = 10

# Common configuration
Vagrant.configure(VAGRANTFILE_API_VERSION) do |config|

  # Before the 'vagrant up' command is started, build docker images:
  config.trigger.before :up, type: :command do |trigger|
    trigger.name = "Build docker images and configuration files"
    trigger.ruby do |env, machine|
      # --- start of Ruby script ---
      # Build load balancer configuration file:
      # Build image for backend nodes:
      puts "Building backend node image:"
      `docker build backend -t "#{BACKEND_IMAGE}"`
      # Build image for the frontend node:
    
      # --- end of Ruby script ---
    end
  end

  config.vm.synced_folder ".", "/vagrant", type: "rsync", rsync__exclude: ".*/"
  config.ssh.insert_key = false

  # Definition of N backends
  (1..BACKENDS_COUNT).each do |i|
    node_ip_addr = "#{BACKENDS[:subnet]}#{BACKENDS[:ip_offset] + i}"
    node_name = "#{BACKENDS[:nameprefix]}#{i}"
    # Definition of BACKEND
    config.vm.define node_name do |s|
      s.vm.network "private_network", ip: node_ip_addr
      s.vm.hostname = node_name
      s.vm.provider "docker" do |d|
        d.build_dir = "backend"
        d.build_args = ["-t", "#{BACKENDS[:image]}"]
        d.name = node_name
        d.has_ssh = true
      end
      s.vm.post_up_message = "Node #{node_name} up and running. You can access the node with 'vagrant ssh #{node_name}'"
    end
  end
end

# EOF
