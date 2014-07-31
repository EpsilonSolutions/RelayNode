package com.mattcorallo.relaynode;

import com.google.bitcoin.core.Block;
import com.google.bitcoin.core.PeerEventListener;
import com.google.bitcoin.core.Transaction;
import com.google.bitcoin.net.NioServer;
import com.google.bitcoin.net.StreamParser;
import com.google.bitcoin.net.StreamParserFactory;

import javax.annotation.Nullable;
import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

public class RelayConnectionListener {
	//TODO: Track memory usage and kill hungry connections

	private final Set<RelayConnection> connectionSet = Collections.synchronizedSet(new HashSet<RelayConnection>());
	private final Set<InetAddress> remoteSet = Collections.synchronizedSet(new HashSet<InetAddress>());

	public RelayConnectionListener(int port, final PeerEventListener clientPeerListener, final RelayNode lineLogger) throws IOException {
		NioServer relayServer = new NioServer(new StreamParserFactory() {
			@Nullable
			@Override
			public StreamParser getNewParser(final InetAddress inetAddress, int port) {
				if (remoteSet.contains(inetAddress))
					return null;

				return new RelayConnection() {
					@Override
					void LogLine(String line) {
						lineLogger.LogLine(line);
					}

					@Override void LogStatsRecv(String lines) { }

					@Override void LogConnected(String line) { }

					@Override
					void receiveBlock(Block b) {
						clientPeerListener.onPreMessageReceived(null, b);
					}

					@Override
					void receiveTransaction(Transaction t) {
						clientPeerListener.onPreMessageReceived(null, t);
					}

					@Override
					public void connectionClosed() {
						connectionSet.remove(this);
						remoteSet.remove(inetAddress);
					}

					@Override
					public void connectionOpened() {
						connectionSet.add(this);
						remoteSet.add(inetAddress);
					}
				};
			}
		}, new InetSocketAddress(port));
		relayServer.startAndWait();
	}

	public void sendTransaction(Transaction t) {
		synchronized (connectionSet) {
			for (RelayConnection connection : connectionSet)
				connection.sendTransaction(t);
		}
	}

	public void sendBlock(Block b) {
		synchronized (connectionSet) {
			for (RelayConnection connection : connectionSet)
				connection.sendBlock(b);
		}
	}

	public Set<InetAddress> getClientSet() {
		synchronized (remoteSet) {
			return new HashSet<>(remoteSet);
		}
	}
}
