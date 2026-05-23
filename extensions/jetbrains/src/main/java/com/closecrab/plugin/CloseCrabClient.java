package com.closecrab.plugin;

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;

public class CloseCrabClient {
    private static final String DEFAULT_URL = "http://localhost:9001";
    private final HttpClient client = HttpClient.newHttpClient();
    private String serverUrl;

    public CloseCrabClient() {
        this.serverUrl = DEFAULT_URL;
    }

    public CloseCrabClient(String url) {
        this.serverUrl = url;
    }

    public String chat(String message) throws Exception {
        String json = "{\"message\":\"" + escapeJson(message) + "\",\"session_id\":\"jetbrains\"}";
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(serverUrl + "/chat"))
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(json))
                .build();
        HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
        return response.body();
    }

    public boolean isConnected() {
        try {
            HttpRequest request = HttpRequest.newBuilder()
                    .uri(URI.create(serverUrl + "/health"))
                    .GET().build();
            HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
            return response.statusCode() == 200;
        } catch (Exception e) {
            return false;
        }
    }

    private String escapeJson(String s) {
        return s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n");
    }
}
