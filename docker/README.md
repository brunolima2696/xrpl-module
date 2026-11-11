# Pistis: build e execução do xrpld

Este fork compila e testa a extensão `PistisLending` e pode publicar sua imagem
no GitHub Container Registry (GHCR). Não depende do repositório do TCC.

## GitHub Actions: execução manual

1. Envie as alterações de protocolo, os testes, `docker/` e o workflow
   `.github/workflows/build-pistis.yml` ao fork. O workflow precisa existir na
   branch padrão para aparecer a opção **Run workflow**. Não envie `.env`,
   `config/`, contas, seeds, estado do ledger ou arquivos de build.
2. Abra **Actions → Build Pistis → Run workflow** e selecione a branch.
3. Na primeira execução, escolha `stage: dependencies`, `build_type: Release`
   e mantenha `publish` desmarcado. Isso prepara e salva as dependências.
4. Depois, execute com `stage: image` e o mesmo `build_type`. As dependências
   serão recuperadas; o job compilará o servidor e executará
   `LoanSet,Vault,Feature,ServerDefinitions`.
5. Para publicar a imagem aprovada, marque `publish` na execução de `image`.
   A publicação é feita no mesmo job, apenas após compilação, testes e
   montagem da imagem bem-sucedidos. Não requer PAT: usa `GITHUB_TOKEN` com
   `packages: write`. Verifique se as políticas do repositório permitem isso.

O novo workflow não roda em `push` ou PR. Os workflows herdados do upstream
foram preservados e mantêm seus próprios gatilhos.

### Recursos, logs e cache

- Runner Linux x64 com Docker. O campo `runner` aceita um label disponível
  na sua conta; o padrão é `ubuntu-24.04`. Nenhum runner pago é provisionado
  automaticamente.
- O preflight exige pelo menos 14 GiB de RAM total e 40 GiB de disco livre.
  São limiares conservadores, não dimensionamento medido nem garantia de
  conclusão. Se falhar, selecione um runner com mais recursos; o runner
  padrão pode não ter disco suficiente. O consumo de memória também depende
  de `build_jobs`; reduza de 4 para 2 se necessário.
- Timeout do job: 330 minutos. Separar dependências e imagem dá uma janela
  própria a cada execução, mas não garante que uma compilação fria caiba
  nesse limite. Se não couber, será necessário outro runner/estratégia.
- `Release` é o padrão remoto para reduzir o espaço de símbolos de debug.
  Isso muda a configuração em relação ao build local `Debug`, não o protocolo.
- Pacotes Conan e cache do compilador são persistidos explicitamente pelo
  Actions. Não dependemos de cache mounts do BuildKit, que não são exportados
  automaticamente pelo cache do Actions. Os objetos do compilador têm limite
  de 2 GB; o cache Conan e a soma dos caches continuam sujeitos às cotas e à
  remoção de caches do GitHub. O cache Docker local não é enviado.
- Dependências concluídas são salvas antes de compilar o servidor. Seus
  diretórios temporários são limpos somente no runner remoto. Uma execução
  interrompida durante a instalação das dependências pode perder esse progresso.
- Os logs são transmitidos diretamente pelo container e armazenados como
  artefatos por 7 dias, junto com commit, configuração e identificação da
  toolchain. O código do protocolo continua precisando passar nos testes;
  configurar o workflow não significa que a imagem já foi validada.

### Imagem publicada

O nome é `ghcr.io/<proprietário-do-fork>/xrpld:pistis-<commit-completo>`.
Na branch padrão também é atualizado o alias `:pistis`. Em outras branches,
use a tag por commit; o alias não é alterado.

O resumo da execução apresenta a referência com digest. Prefira essa
referência no TCC para fixar exatamente a imagem utilizada. A tag por commit
é uma convenção de identificação; a garantia de conteúdo vem do digest.

Pacotes novos podem ser privados. Para permitir `docker pull` sem login,
ajuste a visibilidade do pacote no GHCR. Caso permaneça privado, autentique
o Docker com uma credencial autorizada a ler o pacote.

## Executar uma imagem pronta

Na raiz deste fork:

```bash
export XRPLD_IMAGE=ghcr.io/brunolima2696/xrpld:pistis
docker compose -f docker/compose.yaml pull
docker compose -f docker/compose.yaml up -d --no-build
```

Substitua `XRPLD_IMAGE` pela referência por digest ou por commit exibida no
Actions. Esses comandos pressupõem que a imagem já foi publicada.

O Compose é independente de compilação. Mantém as portas, nomes dos
containers e volumes anteriores; não apague volumes para atualizar a imagem.
O workflow não executa o Compose nem reinicia nós. A configuração do
amendment local continua sendo uma decisão separada; veja
[PistisLending](../README.md).

O repositório do TCC pode consumir essa mesma imagem e manter seu próprio
Compose/configuração, sem carregar o código-fonte deste fork. Este ajuste
não remove o submódulo do TCC nem transfere seus arquivos automaticamente.

## Build local opcional

Na raiz deste fork, sem prefixar os caminhos com `xrpl-module/`:

```bash
docker build --build-arg BUILD_JOBS=4 \
  --build-arg TEST_SUITES=LoanSet,Vault,Feature,ServerDefinitions \
  -f docker/Dockerfile -t xrpld-local:pistis .
```

O build local preserva os IDs dos caches Docker existentes (`xrpld-conan` e
`xrpld-debug-build`); não use `--no-cache` nem remova esses caches. O build
passa a habilitar explicitamente os testes no Conan e no CMake.

Para usar o Compose com compilação local:

```bash
export XRPLD_IMAGE=xrpld-local:pistis
docker compose -f docker/compose.yaml -f docker/compose.build.yaml build xrpld
docker compose -f docker/compose.yaml -f docker/compose.build.yaml up -d
```

`Dockerfile.runtime` é usado pelo CI para empacotar um executável já testado;
nunca o use para publicar um binário cuja origem e testes sejam desconhecidos.
