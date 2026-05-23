using System;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace CloseCrab
{
    public class CloseCrabClient
    {
        private static readonly HttpClient _client = new HttpClient();
        private readonly string _serverUrl;

        public CloseCrabClient(string serverUrl = "http://localhost:9001")
        {
            _serverUrl = serverUrl;
            _client.Timeout = TimeSpan.FromSeconds(120);
        }

        public async Task<string> ChatAsync(string message, string sessionId = "visualstudio")
        {
            var payload = new { message, session_id = sessionId };
            var json = JsonConvert.SerializeObject(payload);
            var content = new StringContent(json, Encoding.UTF8, "application/json");

            try
            {
                var response = await _client.PostAsync($"{_serverUrl}/chat", content);
                var body = await response.Content.ReadAsStringAsync();
                var result = JObject.Parse(body);
                return result["response"]?.ToString() ?? result["error"]?.ToString() ?? "No response";
            }
            catch (Exception ex)
            {
                return $"Error: {ex.Message}";
            }
        }

        public async Task<bool> IsConnectedAsync()
        {
            try
            {
                var response = await _client.GetAsync($"{_serverUrl}/health");
                return response.IsSuccessStatusCode;
            }
            catch { return false; }
        }
    }
}
