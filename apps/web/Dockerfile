FROM node:24-bookworm-slim AS build
WORKDIR /app
RUN npm install --global pnpm@9.15.0
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./
COPY apps/web ./apps/web
RUN pnpm install --frozen-lockfile --filter infhome-web
RUN pnpm web:build

FROM node:24-bookworm-slim AS runtime
ENV NODE_ENV=production \
    HOST=0.0.0.0 \
    PORT=3000 \
    NUXT_DATA_DIR=/var/lib/infhome
WORKDIR /app
RUN mkdir -p /var/lib/infhome && chown node:node /var/lib/infhome
COPY --from=build --chown=node:node /app/apps/web/.output ./.output
USER node
EXPOSE 3000
HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
  CMD node -e "fetch('http://127.0.0.1:3000/health').then(r => process.exit(r.ok ? 0 : 1)).catch(() => process.exit(1))"
CMD ["node", ".output/server/index.mjs"]
