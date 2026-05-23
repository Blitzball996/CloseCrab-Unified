package com.closecrab.plugin;

import com.intellij.openapi.project.Project;
import com.intellij.openapi.wm.ToolWindow;
import com.intellij.openapi.wm.ToolWindowFactory;
import com.intellij.ui.content.Content;
import com.intellij.ui.content.ContentFactory;
import org.jetbrains.annotations.NotNull;

import javax.swing.*;
import java.awt.*;

public class ChatToolWindowFactory implements ToolWindowFactory {
    @Override
    public void createToolWindowContent(@NotNull Project project, @NotNull ToolWindow toolWindow) {
        ChatPanel chatPanel = new ChatPanel();
        Content content = ContentFactory.getInstance().createContent(chatPanel, "Chat", false);
        toolWindow.getContentManager().addContent(content);
    }
}

class ChatPanel extends JPanel {
    private final JTextArea chatArea;
    private final JTextField inputField;
    private final CloseCrabClient client;

    public ChatPanel() {
        super(new BorderLayout());
        client = new CloseCrabClient();

        chatArea = new JTextArea();
        chatArea.setEditable(false);
        chatArea.setLineWrap(true);
        chatArea.setWrapStyleWord(true);
        add(new JScrollPane(chatArea), BorderLayout.CENTER);

        JPanel inputPanel = new JPanel(new BorderLayout());
        inputField = new JTextField();
        JButton sendBtn = new JButton("Send");

        inputField.addActionListener(e -> sendMessage());
        sendBtn.addActionListener(e -> sendMessage());

        inputPanel.add(inputField, BorderLayout.CENTER);
        inputPanel.add(sendBtn, BorderLayout.EAST);
        add(inputPanel, BorderLayout.SOUTH);

        // Connection check
        if (!client.isConnected()) {
            chatArea.append("[CloseCrab] Backend not running. Start closecrab-unified.exe first.\n");
        } else {
            chatArea.append("[CloseCrab] Connected to backend.\n");
        }
    }

    private void sendMessage() {
        String msg = inputField.getText().trim();
        if (msg.isEmpty()) return;
        inputField.setText("");
        chatArea.append("\nYou: " + msg + "\n");

        new Thread(() -> {
            try {
                String response = client.chat(msg);
                SwingUtilities.invokeLater(() -> chatArea.append("\nCloseCrab: " + response + "\n"));
            } catch (Exception e) {
                SwingUtilities.invokeLater(() -> chatArea.append("\n[Error] " + e.getMessage() + "\n"));
            }
        }).start();
    }
}
