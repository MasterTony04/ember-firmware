# Setup and tear down for client tests
# Automatically included in any example groups tagged with :client

RSpec.shared_context 'client context', :client do
  include PrintEngineHelperAsync
  include LogHelperAsync

  # Value for retry timers
  let(:retry_interval) { 0 }
  
  before do
    create_command_pipe_async
    create_command_response_pipe_async
    open_command_response_pipe_async

    # Open and watch command pipe
    watch_command_pipe_async

    # Set argument to true to print client log output to stdout
    watch_log_async($client_log_enable)
    
    # Make printer reachable by default
    set_settings_async(server_url: dummy_server.url)
  end

  after do
    stop_client_async
    close_command_pipe_async
    close_command_response_pipe_async
  end
end